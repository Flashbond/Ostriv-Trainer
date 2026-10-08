#include "OffsetResolver.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include "../Core/RemoteMemory.h"
#include "../Offsets/OstrivOffsets.h"   // keep the include paths your file already has for these two

namespace
{
    // How far around the last-known address the cheap scan looks. The
    // 0.5.9.62 data shift was ~0x1E30, so this is generous.
    constexpr uintptr_t kNarrowScanRadius = 0x40000;

    bool IsPlausiblePointer(uintptr_t p)
    {
        return p >= Ostriv::MIN_VALID_POINTER && p <= Ostriv::MAX_VALID_POINTER;
    }

    bool StartsWithAscii(RemoteMemory& m, uintptr_t address, const char* prefix)
    {
        const size_t length = std::strlen(prefix);
        char text[32] = {};
        if (length >= sizeof(text) || !m.ReadBytes(address, text, length))
            return false;
        return std::memcmp(text, prefix, length) == 0;
    }

    // None of these prove an address is right; they reject the wrong ones.
    // A stale offset usually still reads *some* number and *some* pointer,
    // which is how Connect() once "succeeded" with an empty building list.

    // array[i] -> building object -> +0x18 id pointer -> "building_..."
    bool LooksLikeBuildingsTable(RemoteMemory& m, uintptr_t countSlot, uintptr_t arraySlot)
    {
        int32_t count = 0;
        uintptr_t array = 0;
        if (!m.Read(countSlot, count) || !m.Read(arraySlot, array)) return false;
        if (count <= 0 || count > Ostriv::INGAME_BUILDINGS_TABLE_MAX_COUNT) return false;
        if (!IsPlausiblePointer(array)) return false;

        const int limit = count < 256 ? count : 256;
        for (int i = 0; i < limit; ++i)
        {
            uintptr_t object = 0, idPtr = 0;
            if (!m.Read(array + static_cast<uintptr_t>(i) * sizeof(uintptr_t), object) || !IsPlausiblePointer(object))
                continue;
            if (!m.Read(object + Ostriv::INGAME_BUILDING_ID_PTR_OFFSET, idPtr) || !IsPlausiblePointer(idPtr))
                continue;
            if (StartsWithAscii(m, idPtr, "building_"))
                return true;
        }
        return false;
    }

    // row -> +0x00 id pointer -> "building_..."
    bool LooksLikeDictionary(RemoteMemory& m, uintptr_t countSlot, uintptr_t pointerSlot)
    {
        int32_t count = 0;
        uintptr_t table = 0;
        if (!m.Read(countSlot, count) || !m.Read(pointerSlot, table)) return false;
        if (count <= 0 || count > Ostriv::BUILDING_DICTIONARY_TABLE_MAX_COUNT) return false;
        if (!IsPlausiblePointer(table)) return false;

        const int limit = count < 64 ? count : 64;
        for (int i = 0; i < limit; ++i)
        {
            uintptr_t idPtr = 0;
            uintptr_t row = table + static_cast<uintptr_t>(i) * Ostriv::BUILDING_DICTIONARY_ENTRY_SIZE;
            if (!m.Read(row + Ostriv::BUILDING_DICTIONARY_ID_PTR_OFFSET, idPtr) || !IsPlausiblePointer(idPtr))
                continue;
            if (StartsWithAscii(m, idPtr, "building_"))
                return true;
        }
        return false;
    }

    // Entry 0 is the game's own "None" placeholder; most other entries must
    // look like a (pointer, short length) pair.
    bool LooksLikeResourceTable(RemoteMemory& m, uintptr_t table)
    {
        if (!table) return false;

        uintptr_t namePtr = 0;
        int32_t length = 0;
        if (!m.Read(table + Ostriv::RESOURCE_NAME_PTR_OFFSET, namePtr) ||
            !m.Read(table + Ostriv::RESOURCE_NAME_LENGTH_OFFSET, length))
            return false;
        if (!IsPlausiblePointer(namePtr) || length < 4 || length > 5) return false;

        wchar_t text[4] = {};
        if (!m.ReadBytes(namePtr, text, sizeof(text)) || std::wmemcmp(text, L"None", 4) != 0)
            return false;

        int plausible = 0;
        for (int32_t id = 1; id < Ostriv::RESOURCE_TABLE_COUNT - 1; ++id)
        {
            uintptr_t entry = table + static_cast<uintptr_t>(id) * Ostriv::RESOURCE_ENTRY_SIZE;
            uintptr_t ptr = 0;
            int32_t len = 0;
            if (m.Read(entry + Ostriv::RESOURCE_NAME_PTR_OFFSET, ptr) &&
                m.Read(entry + Ostriv::RESOURCE_NAME_LENGTH_OFFSET, len) &&
                IsPlausiblePointer(ptr) && len > 0 && len <= 64)
                ++plausible;
        }
        return plausible >= (Ostriv::RESOURCE_TABLE_COUNT * 3) / 4;
    }


    // Weak on purpose: there is no independent way to prove this is the
    // money field without knowing the player's balance. Compare the money
    // label in the UI against the game whenever the report says anything
    // other than "signature".
    bool LooksLikeMoneySlot(RemoteMemory& m, uintptr_t slot)
    {
        uintptr_t state = 0;
        if (!m.Read(slot, state) || !IsPlausiblePointer(state)) return false;

        double money = 0.0;
        if (!m.Read(state + Ostriv::MONEY_OFFSET, money)) return false;
        return std::isfinite(money) && std::fabs(money) < 1e13;
    }

    // A JMP left behind by a previous session that crashed before
    // Uninstall(). Only accepted if it jumps into memory WE allocated
    // (private, not part of the exe image); the game's own E9 jumps stay
    // inside its image.
    bool IsOwnLeftoverPatch(RemoteMemory& m, uintptr_t address)
    {
        uint8_t opcode = 0;
        int32_t rel = 0;
        if (!m.Read(address, opcode) || opcode != 0xE9 || !m.Read(address + 1, rel)) return false;

        uintptr_t target = address + 5 + static_cast<intptr_t>(rel);
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(m.GetProcessHandle(), reinterpret_cast<LPCVOID>(target), &info, sizeof(info)) == 0)
            return false;

        return info.Type == MEM_PRIVATE;
    }

    bool HookSiteLooksRight(RemoteMemory& m, uintptr_t address, const uint8_t* expected, size_t size)
    {
        if (!address) return false;
        std::vector<uint8_t> current(size);
        if (!m.ReadBytes(address, current.data(), size)) return false;
        return std::memcmp(current.data(), expected, size) == 0 || IsOwnLeftoverPatch(m, address);
    }
}

OffsetResolver::OffsetResolver(RemoteMemory& memory)
    : m_memory(memory)
{
}

uintptr_t OffsetResolver::Find(const char* signature) const
{
    if (!signature || signature[0] == '\0')
        return 0;
    return m_memory.FindPattern(signature);
}

uintptr_t OffsetResolver::RipTarget(uintptr_t instruction) const
{
    // Every RIP-relative instruction we sign is 7 bytes with the disp32 at byte 3.
    return m_memory.ResolveRipRelative(instruction, 3, 7);
}

void OffsetResolver::LoadSections() const
{
    if (m_sectionsLoaded)
        return;
    m_sectionsLoaded = true;

    const uintptr_t base = m_memory.GetModuleBase();

    IMAGE_DOS_HEADER dos{};
    if (!m_memory.Read(base, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return;

    IMAGE_NT_HEADERS64 nt{};
    if (!m_memory.Read(base + dos.e_lfanew, nt) || nt.Signature != IMAGE_NT_SIGNATURE)
        return;

    m_imageSize = nt.OptionalHeader.SizeOfImage;

    const uintptr_t sectionTable = base + dos.e_lfanew +
        offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;

    for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i)
    {
        IMAGE_SECTION_HEADER section{};
        if (!m_memory.Read(sectionTable + static_cast<uintptr_t>(i) * sizeof(IMAGE_SECTION_HEADER), section))
            break;

        const uintptr_t begin = base + section.VirtualAddress;
        const uintptr_t size = (std::max)(section.Misc.VirtualSize, section.SizeOfRawData);

        if (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)
            m_codeSections.push_back(Range{ begin, begin + size });
        else if (section.Characteristics & IMAGE_SCN_MEM_WRITE)
            m_sections.push_back(Range{ begin, begin + size });   // globals live here
    }
}

const std::vector<OffsetResolver::Range>& OffsetResolver::WritableSections() const
{
    LoadSections();
    return m_sections;
}

const std::vector<OffsetResolver::Range>& OffsetResolver::CodeSections() const
{
    LoadSections();
    return m_codeSections;
}

std::vector<uintptr_t> OffsetResolver::FindAllBytes(const uint8_t* needle, size_t size) const
{
    std::vector<uintptr_t> hits;
    constexpr size_t kChunk = 0x10000;
    std::vector<uint8_t> buffer(kChunk + size);

    for (const Range& range : CodeSections())
    {
        for (uintptr_t start = range.begin; start < range.end; start += kChunk)
        {
            // Read a little past the chunk so a match straddling the
            // boundary is still complete in the buffer.
            const size_t want = (std::min)(static_cast<size_t>(range.end - start), kChunk + size);
            if (want < size || !m_memory.ReadBytes(start, buffer.data(), want))
                continue;

            // Only offsets inside this chunk count, so a boundary match is not reported twice.
            for (size_t i = 0; i < kChunk && i + size <= want; ++i)
            {
                if (std::memcmp(buffer.data() + i, needle, size) == 0)
                    hits.push_back(start + i);
            }
        }
    }
    return hits;
}

// The selection hook sits on the function that walks a building's inventory
// (count at +0x198, array at +0x1A0). Both offsets are above 0x7F, so they
// appear in the code as 4-byte little-endian displacements.
bool OffsetResolver::BodyTouchesInventory(uintptr_t function) const
{
    constexpr size_t kWindow = 0x400;
    std::vector<uint8_t> body(kWindow);
    if (!m_memory.ReadBytes(function, body.data(), kWindow))
        return false;

    const uint8_t countDisp[4] = { 0x98, 0x01, 0x00, 0x00 };
    const uint8_t arrayDisp[4] = { 0xA0, 0x01, 0x00, 0x00 };

    auto contains = [&](const uint8_t* needle) {
        return std::search(body.begin(), body.end(), needle, needle + 4) != body.end();
        };
    return contains(countDisp) && contains(arrayDisp);
}

bool OffsetResolver::ScanWritableData(size_t window, uintptr_t around, uintptr_t radius,
    const std::function<bool(uintptr_t, const uint8_t*)>& visit) const
{
    constexpr size_t kChunk = 0x10000;
    std::vector<uint8_t> buffer(kChunk + window);

    for (const Range& section : WritableSections())
    {
        Range range = section;

        if (around != 0)
        {
            const uintptr_t low = around > radius ? around - radius : 0;
            const uintptr_t high = around + radius;

            range.begin = (std::max)(range.begin, low);
            range.end = (std::min)(range.end, high);
            if (range.begin >= range.end)
                continue;

            range.begin &= ~static_cast<uintptr_t>(7); // the scan steps in 8-byte strides
        }

        for (uintptr_t chunkStart = range.begin; chunkStart < range.end; chunkStart += kChunk)
        {
            // Read a little past the chunk so a candidate straddling the
            // boundary is still fully inside the buffer.
            const size_t want = (std::min)(static_cast<size_t>(range.end - chunkStart), kChunk + window);
            if (!m_memory.ReadBytes(chunkStart, buffer.data(), want))
                continue; // unreadable page(s): skip this chunk

            for (size_t offset = 0; offset < kChunk && offset + window <= want; offset += 8)
            {
                if (visit(chunkStart + offset, buffer.data() + offset))
                    return true;
            }
        }
    }
    return false;
}

std::vector<OffsetResolver::Candidate> OffsetResolver::CollectCountPointerPairs(
    const Validator& isValid, uintptr_t around, uintptr_t radius) const
{
    std::vector<Candidate> candidates;

    ScanWritableData(16, around, radius, [&](uintptr_t address, const uint8_t* p) {
        int32_t count = 0;
        uint64_t pointer = 0;
        std::memcpy(&count, p, sizeof(count));
        std::memcpy(&pointer, p + 8, sizeof(pointer));

        // Cheap prefilter first: the validators do remote reads.
        if (count <= 0 || count > Ostriv::INGAME_BUILDINGS_TABLE_MAX_COUNT || !IsPlausiblePointer(pointer))
            return false;

        Slots candidate{ address, address + 8 };
        if (isValid(candidate))
            candidates.push_back(Candidate{ candidate, count });

        return false; // keep going: look-alike neighbours are exactly what we want to see
        });

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.slots.first < b.slots.first; });
    return candidates;
}

OffsetResolver::Slots OffsetResolver::ScanCountPointerPairs(const Validator& isValid, uintptr_t around, uintptr_t radius)
{
    const std::vector<Candidate> candidates = CollectCountPointerPairs(isValid, around, radius);

    if (candidates.empty())
        return Slots{};

    if (candidates.size() > 1)
    {
        // Several game vectors of objects sharing one base class pass the
        // same content check, and picking by count or position was shown to
        // choose wrong. Refuse instead of guessing.
        AddNote(L"a scan found several look-alike count+pointer candidates and chose none (see the candidate list)");
        return Slots{};
    }

    return candidates.front().slots;
}

std::vector<uintptr_t> OffsetResolver::CollectResourceTables() const
{
    std::vector<uintptr_t> found;

    ScanWritableData(16, 0, 0, [&](uintptr_t address, const uint8_t* p) {
        uint64_t namePtr = 0;
        int32_t length = 0;
        std::memcpy(&namePtr, p, sizeof(namePtr));
        std::memcpy(&length, p + 8, sizeof(length));

        if (!IsPlausiblePointer(namePtr) || length < 4 || length > 5)
            return false;
        if (LooksLikeResourceTable(m_memory, address))
            found.push_back(address);
        return false;
        });

    return found;
}

std::wstring OffsetResolver::FormatList(const std::wstring& title,
    std::vector<std::pair<uintptr_t, std::wstring>> rows, uintptr_t chosenAddress) const
{
    const uintptr_t base = m_memory.GetModuleBase();
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::wstring text = title + L": " + std::to_wstring(rows.size()) + L" found";
    for (const auto& row : rows)
    {
        wchar_t line[192];
        swprintf_s(line, L"\n    0x%llX  %s%s", static_cast<unsigned long long>(row.first - base),
            row.second.c_str(), (chosenAddress && row.first == chosenAddress) ? L"   <-- in use" : L"");
        text += line;
    }
    return text;
}

void OffsetResolver::AddNote(const std::wstring& note)
{
    // The narrow and the full scan would otherwise both add the same line.
    if (std::find(m_notes.begin(), m_notes.end(), note) == m_notes.end())
        m_notes.push_back(note);
}

void OffsetResolver::CollectDiagnostics()
{
    auto countPointerRows = [this](const Validator& valid) {
        std::vector<std::pair<uintptr_t, std::wstring>> rows;
        for (const Candidate& c : CollectCountPointerPairs(valid, 0, 0))
            rows.emplace_back(c.slots.first, L"n=" + std::to_wstring(c.count));
        return rows;
        };

    // These lists come from the content check, not from the signatures, so
    // they are also useful for items a signature found on its own.
    m_details.push_back(FormatList(L"buildings table, count slots passing the content check",
        countPointerRows([this](const Slots& s) { return LooksLikeBuildingsTable(m_memory, s.first, s.second); }),
        m_result.buildingsCountSlot));

    m_details.push_back(FormatList(L"building dictionary, count slots passing the content check",
        countPointerRows([this](const Slots& s) { return LooksLikeDictionary(m_memory, s.first, s.second); }),
        m_result.dictionaryCountSlot));

    {
        std::vector<std::pair<uintptr_t, std::wstring>> rows;
        for (uintptr_t table : CollectResourceTables())
            rows.emplace_back(table, L"");
        m_details.push_back(FormatList(L"resource table, entry 0 is \"None\"", rows, m_result.resourceTable));
    }

    {
        std::vector<std::pair<uintptr_t, std::wstring>> rows;
        for (uintptr_t site : FindAllBytes(Ostriv::CAMERA_ORIGINAL_BYTES, Ostriv::CAMERA_HOOK_SIZE))
            rows.emplace_back(site, L"");
        m_details.push_back(FormatList(L"camera hook, sites holding the hooked instruction", rows, m_result.cameraHook));
    }

    {
        // movsd xmm0,[rax+MONEY_OFFSET] preceded by mov rax,[rip+slot]
        uint8_t needle[8] = { 0xF2, 0x0F, 0x10, 0x80 };
        const uint32_t disp = static_cast<uint32_t>(Ostriv::MONEY_OFFSET);
        std::memcpy(needle + 4, &disp, sizeof(disp));

        std::vector<std::pair<uintptr_t, std::wstring>> rows;
        for (uintptr_t site : FindAllBytes(needle, sizeof(needle)))
        {
            uint8_t head[3] = {};
            if (!m_memory.ReadBytes(site - 7, head, sizeof(head)) || head[0] != 0x48 || head[1] != 0x8B || head[2] != 0x05)
                continue;

            const uintptr_t slot = RipTarget(site - 7);
            rows.emplace_back(slot, LooksLikeMoneySlot(m_memory, slot) ? L"passes the check" : L"fails the check");
        }
        m_details.push_back(FormatList(L"money, slots read right before the money field access (current MONEY_OFFSET)",
            rows, m_result.moneyStateSlot));
    }
}

uintptr_t OffsetResolver::ScanResourceTable(uintptr_t around, uintptr_t radius) const
{
    uintptr_t found = 0;

    ScanWritableData(16, around, radius, [&](uintptr_t address, const uint8_t* p) {
        uint64_t namePtr = 0;
        int32_t length = 0;
        std::memcpy(&namePtr, p, sizeof(namePtr));
        std::memcpy(&length, p + 8, sizeof(length));

        // Entry 0 must look like {pointer to L"None", length 4 or 5}.
        if (!IsPlausiblePointer(namePtr) || length < 4 || length > 5)
            return false;
        if (!LooksLikeResourceTable(m_memory, address))
            return false;

        found = address;
        return true;
        });

    return found;
}

OffsetResolver::Slots OffsetResolver::Choose(const Item& item)
{
    Slots chosen;
    Source source = Source::None;
    bool overrideRejected = false;

    // 1. Hand-typed hint from OstrivOffsets.h. Validated like everything
    //    else: a stale override that no longer checks out is ignored.
    if (item.manual.first)
    {
        if (item.isValid(item.manual))
        {
            chosen = item.manual;
            source = Source::Manual;
        }
        else
        {
            overrideRejected = true;
        }
    }

    // 2. Signature.
    if (source == Source::None && item.fromSignature.first && item.isValid(item.fromSignature))
    {
        chosen = item.fromSignature;
        source = Source::Signature;
    }

    // 2b. A hook patch of ours left in the game by a session that ended
    //     without Disconnect. Both ends of the patch were verified when it
    //     was found, which makes it more trustworthy than any last-known value.
    if (source == Source::None && item.fromLeftover.first && item.isValid(item.fromLeftover))
    {
        chosen = item.fromLeftover;
        source = Source::Leftover;
    }

    // 3. Last-known value shifted by whatever an earlier item already proved.
    if (source == Source::None && item.useDelta && m_hasDelta && m_delta != 0)
    {
        const uintptr_t shift = static_cast<uintptr_t>(m_delta);
        Slots shifted{ item.lastKnown.first + shift, item.lastKnown.second ? item.lastKnown.second + shift : 0 };
        if (item.isValid(shifted))
        {
            chosen = shifted;
            source = Source::Delta;
        }
    }

    // 4. Last-known value as is.
    if (source == Source::None && item.lastKnown.first && item.isValid(item.lastKnown))
    {
        chosen = item.lastKnown;
        source = Source::Fallback;
    }

    // 5. Scan near the last-known address, then everywhere.
    if (source == Source::None && item.scan && item.lastKnown.first)
    {
        Slots found = item.scan(item.lastKnown.first, kNarrowScanRadius);
        if (found.first && item.isValid(found))
        {
            chosen = found;
            source = Source::NarrowScan;
        }
    }

    if (source == Source::None && item.scan)
    {
        Slots found = item.scan(0, 0);
        if (found.first && item.isValid(found))
        {
            chosen = found;
            source = Source::FullScan;
        }
    }

    // Learn the data-table shift only from independent evidence (signature
    // or scan), never from a value that was itself a guess. A zero shift
    // teaches nothing, so keep waiting for an item that actually moved.
    const bool independent = source == Source::Signature;
    if (item.useDelta && !m_hasDelta && independent && chosen.first != item.lastKnown.first)
    {
        m_delta = static_cast<intptr_t>(chosen.first) - static_cast<intptr_t>(item.lastKnown.first);
        m_hasDelta = true;
    }

    Entry entry;
    entry.name = item.name;
    entry.source = source;
    entry.overrideRejected = overrideRejected;
    entry.chosen = chosen;
    entry.lastKnown = item.lastKnown;
    entry.firstConstant = item.firstConstant;
    entry.secondConstant = item.secondConstant;
    m_entries.push_back(entry);

    return chosen;
}

ResolvedOffsets OffsetResolver::Resolve()
{
    m_notes.clear();
    m_details.clear();
    m_warnings.clear();
    m_entries.clear();
    m_hasDelta = false;
    m_delta = 0;

    ResolvedOffsets result;
    const uintptr_t base = m_memory.GetModuleBase();

    // 0 stays 0 ("no override"), anything else becomes an absolute address.
    auto absolute = [base](uintptr_t rva) -> uintptr_t { return rva ? base + rva : 0; };

    // 1. Live building array: first = count slot, second = array-pointer slot.
    {
        Item item;
        item.name = L"buildings table";
        item.firstConstant = L"INGAME_BUILDINGS_TABLE_OFFSET";
        item.useDelta = true;

        if (const uintptr_t manual = absolute(Ostriv::INGAME_BUILDINGS_TABLE_OVERRIDE))
            item.manual = Slots{ manual + Ostriv::INGAME_BUILDINGS_TABLE_COUNT_OFFSET,
                                 manual + Ostriv::INGAME_BUILDINGS_TABLE_ARRAY_OFFSET };

        if (uintptr_t match = Find(Ostriv::INGAME_BUILDINGS_TABLE_SIGNATURE))
        {
            item.fromSignature.first = RipTarget(match + Ostriv::INGAME_BUILDINGS_TABLE_SIG_COUNT_INSN);
            item.fromSignature.second = RipTarget(match + Ostriv::INGAME_BUILDINGS_TABLE_SIG_ARRAY_INSN);
        }

        const uintptr_t tableBase = base + Ostriv::INGAME_BUILDINGS_TABLE_OFFSET;
        item.lastKnown = Slots{ tableBase + Ostriv::INGAME_BUILDINGS_TABLE_COUNT_OFFSET,
                                tableBase + Ostriv::INGAME_BUILDINGS_TABLE_ARRAY_OFFSET };

        item.isValid = [this](const Slots& s) { return LooksLikeBuildingsTable(m_memory, s.first, s.second); };
        item.scan = [this, valid = item.isValid](uintptr_t around, uintptr_t radius) {
            return ScanCountPointerPairs(valid, around, radius);
            };

        Slots chosen = Choose(item);
        result.buildingsCountSlot = chosen.first;
        result.buildingsArraySlot = chosen.second;
    }

    // 2. Building dictionary: first = count slot, second = pointer slot.
    {
        Item item;
        item.name = L"building dictionary";
        item.firstConstant = L"BUILDING_DICTIONARY_TABLE_COUNT_OFFSET";
        item.secondConstant = L"BUILDING_DICTIONARY_TABLE_POINTER_OFFSET";
        item.useDelta = true;

        if (const uintptr_t manual = absolute(Ostriv::BUILDING_DICTIONARY_TABLE_OVERRIDE))
            item.manual = Slots{ manual, manual + (Ostriv::BUILDING_DICTIONARY_TABLE_POINTER_OFFSET -
                                                   Ostriv::BUILDING_DICTIONARY_TABLE_COUNT_OFFSET) };

        if (uintptr_t match = Find(Ostriv::BUILDING_DICTIONARY_TABLE_SIGNATURE))
        {
            item.fromSignature.first = RipTarget(match + Ostriv::BUILDING_DICTIONARY_TABLE_SIG_COUNT_INSN);
            item.fromSignature.second = RipTarget(match + Ostriv::BUILDING_DICTIONARY_TABLE_SIG_POINTER_INSN);
        }

        item.lastKnown = Slots{ base + Ostriv::BUILDING_DICTIONARY_TABLE_COUNT_OFFSET,
                                base + Ostriv::BUILDING_DICTIONARY_TABLE_POINTER_OFFSET };

        item.isValid = [this](const Slots& s) { return LooksLikeDictionary(m_memory, s.first, s.second); };
        item.scan = [this, valid = item.isValid](uintptr_t around, uintptr_t radius) {
            return ScanCountPointerPairs(valid, around, radius);
            };

        Slots chosen = Choose(item);
        result.dictionaryCountSlot = chosen.first;
        result.dictionaryPointerSlot = chosen.second;
    }

    // 3. Resource table: address of entry 0. No signature; the scan looks
    //    for a table whose entry 0 is "None".
    {
        Item item;
        item.name = L"resource table";
        item.firstConstant = L"RESOURCE_TABLE_OFFSET";
        item.useDelta = true;

        item.manual.first = absolute(Ostriv::RESOURCE_TABLE_OVERRIDE);
        item.lastKnown.first = base + Ostriv::RESOURCE_TABLE_OFFSET;

        item.isValid = [this](const Slots& s) { return LooksLikeResourceTable(m_memory, s.first); };
        item.scan = [this](uintptr_t around, uintptr_t radius) {
            return Slots{ ScanResourceTable(around, radius), 0 };
            };

        result.resourceTable = Choose(item).first;
    }

    // Resource table element count. The destructor-call shape alone is
    // ambiguous (several unrelated tables share entry size 0x10), so every
    // match's RIP target is compared against the table address we already
    // resolved above — only the one that actually points at our table
    // counts, and we read the literal count right there.
    {
        int32_t discovered = Ostriv::RESOURCE_TABLE_COUNT;
        Source source = Source::Fallback;

        if (result.resourceTable)
        {
            for (uintptr_t match : FindAllPattern(Ostriv::RESOURCE_TABLE_COUNT_SIGNATURE))
            {
                uintptr_t target = RipTarget(match + 18); // the RCX LEA inside this call shape
                if (target != result.resourceTable)
                    continue;

                int32_t value = 0;
                if (m_memory.Read(match + Ostriv::RESOURCE_TABLE_COUNT_SIG_IMM_OFFSET, value) &&
                    value > 0 && value <= Ostriv::RESOURCE_TABLE_COUNT_SANITY_MAX)
                {
                    discovered = value;
                    source = Source::Signature;
                }
                break; // the table address is unique, so this is the only real candidate
            }
        }

        result.resourceTableCount = discovered;

        Entry entry;
        entry.name = L"resource table count";
        entry.source = source;
        entry.isScalar = true;
        entry.chosen = Slots{ static_cast<uintptr_t>(discovered), 0 };
        entry.lastKnown = Slots{ static_cast<uintptr_t>(Ostriv::RESOURCE_TABLE_COUNT), 0 };
        entry.firstConstant = L"RESOURCE_TABLE_COUNT";
        m_entries.push_back(entry);
    }

    // 4. Money state pointer slot. No scan: nothing to validate a hit against.
    {
        Item item;
        item.name = L"money";
        item.firstConstant = L"MONEY_STATE_POINTER_OFFSET";
        item.useDelta = true;

        item.manual.first = absolute(Ostriv::MONEY_STATE_POINTER_OVERRIDE);

        if (uintptr_t match = Find(Ostriv::MONEY_STATE_POINTER_SIGNATURE))
            item.fromSignature.first = RipTarget(match + Ostriv::MONEY_STATE_POINTER_SIG_INSN);

        item.lastKnown.first = base + Ostriv::MONEY_STATE_POINTER_OFFSET;
        item.isValid = [this](const Slots& s) { return LooksLikeMoneySlot(m_memory, s.first); };

        result.moneyStateSlot = Choose(item).first;
    }

    // 5. Camera hook. Code, not data: the data-table shift does not apply,
    //    and there is no scan.
    {
        Item item;
        item.name = L"camera hook";
        item.firstConstant = L"CAMERA_UPDATE_RVA";

        item.manual.first = absolute(Ostriv::CAMERA_HOOK_OVERRIDE);
        item.fromSignature.first = Find(Ostriv::CAMERA_HOOK_SIGNATURE);
        item.lastKnown.first = base + Ostriv::CAMERA_UPDATE_RVA;
        item.isValid = [this](const Slots& s) {
            return HookSiteLooksRight(m_memory, s.first, Ostriv::CAMERA_ORIGINAL_BYTES, Ostriv::CAMERA_HOOK_SIZE);
            };

        // A patch of ours may still sit in the game from a session that
        // ended without Disconnect. The signature cannot see it (its bytes
        // are overwritten) and the header address may be stale, so find it
        // from the trampoline instead.
        const std::vector<uintptr_t> leftovers =
            FindLeftoverHookSites(Ostriv::CAMERA_ORIGINAL_BYTES, Ostriv::CAMERA_HOOK_SIZE);
        if (leftovers.size() == 1)
            item.fromLeftover.first = leftovers.front();
        else if (leftovers.size() > 1)
            m_warnings.push_back(L"camera hook: several leftover patches found, none used");

        result.cameraHook = Choose(item).first;
    }

    // 6. Selection hook: many functions open with the same 5 bytes; only
    //    the one whose body reads the inventory fields counts. With several
    //    matches we do not guess and the chain falls back to last-known.
    {
        Item item;
        item.name = L"selection hook";
        item.firstConstant = L"SELECTION_HOOK_RVA";

        item.manual.first = absolute(Ostriv::SELECTION_HOOK_OVERRIDE);
        item.lastKnown.first = base + Ostriv::SELECTION_HOOK_RVA;

        const std::vector<uintptr_t> sites =
            FindAllBytes(Ostriv::SELECTION_HOOK_ORIGINAL_BYTES, Ostriv::SELECTION_HOOK_SIZE);

        std::vector<std::pair<uintptr_t, std::wstring>> rows;
        std::vector<uintptr_t> matching;
        for (uintptr_t site : sites)
        {
            const bool reads = BodyTouchesInventory(site);
            if (reads)
                matching.push_back(site);
            rows.emplace_back(site, reads ? L"reads the inventory fields" : L"");
        }

        wchar_t note[160];
        swprintf_s(note, L"selection hook: %zu sites share its opening bytes, %zu of them read the inventory fields",
            sites.size(), matching.size());
        AddNote(note);

        if (matching.size() == 1)
            item.fromSignature.first = matching.front();

        item.isValid = [this](const Slots& s) {
            return HookSiteLooksRight(m_memory, s.first, Ostriv::SELECTION_HOOK_ORIGINAL_BYTES, Ostriv::SELECTION_HOOK_SIZE);
            };

        const std::vector<uintptr_t> leftovers =
            FindLeftoverHookSites(Ostriv::SELECTION_HOOK_ORIGINAL_BYTES, Ostriv::SELECTION_HOOK_SIZE);
        if (leftovers.size() == 1)
            item.fromLeftover.first = leftovers.front();
        else if (leftovers.size() > 1)
            m_warnings.push_back(L"selection hook: several leftover patches found, none used");

        result.selectionHook = Choose(item).first;

        m_details.push_back(FormatList(L"selection hook, sites sharing the opening bytes", rows, result.selectionHook));
    }

    m_result = result;
    return result;
}

std::wstring OffsetResolver::ProblemSummary() const
{
    std::wstring missing;
    for (const auto& entry : m_entries)
    {
        if (entry.source != Source::None) continue;
        if (!missing.empty()) missing += L", ";
        missing += entry.name;
    }

    return missing.empty() ? std::wstring() : L"offsets not found: " + missing;
}

std::wstring OffsetResolver::AttentionSummary() const
{
    auto append = [](std::wstring& list, const std::wstring& name) {
        if (!list.empty()) list += L", ";
        list += name;
        };

    std::wstring moved, manual, rejected, recovered;
    for (const auto& entry : m_entries)
    {
        if (entry.overrideRejected) append(rejected, entry.name);
        if (entry.source == Source::Manual) append(manual, entry.name);
        if (entry.source == Source::Leftover) append(recovered, entry.name);
        if (entry.source == Source::Delta || entry.source == Source::NarrowScan || entry.source == Source::FullScan)
            append(moved, entry.name);
    }

    std::wstring text;
    auto addPart = [&text](const std::wstring& part) {
        if (!text.empty()) text += L"; ";
        text += part;
        };

    if (!moved.empty()) addPart(L"offsets moved, verify: " + moved);
    if (!manual.empty()) addPart(L"manual override in use: " + manual);
    if (!rejected.empty()) addPart(L"override rejected: " + rejected);
    if (!recovered.empty()) addPart(L"recovered a hook patch left by a previous session: " + recovered);

    for (const auto& warning : m_warnings)
        addPart(warning);

    return text;
}

std::vector<uintptr_t> OffsetResolver::FindLeftoverHookSites(const uint8_t* original, size_t size) const
{
    std::vector<uintptr_t> sites;

    LoadSections(); // also learns the image size
    const uintptr_t imageBegin = m_memory.GetModuleBase();
    const uintptr_t imageEnd = imageBegin + m_imageSize;
    if (m_imageSize == 0 || size < 5)
        return sites;

    auto isExecutable = [](DWORD protect) {
        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            return false;
        return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
        };

    constexpr SIZE_T kMaxRegion = 0x1000000; // our caves are tiny; skip anything big
    uintptr_t address = 0x10000;

    MEMORY_BASIC_INFORMATION info{};
    while (address < Ostriv::MAX_VALID_POINTER &&
        VirtualQueryEx(m_memory.GetProcessHandle(), reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info))
    {
        const uintptr_t regionBegin = reinterpret_cast<uintptr_t>(info.BaseAddress);
        address = regionBegin + info.RegionSize; // always advance, whatever the checks below decide

        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE ||
            !isExecutable(info.Protect) || info.RegionSize > kMaxRegion)
            continue;

        std::vector<uint8_t> buffer(info.RegionSize);
        if (!m_memory.ReadBytes(regionBegin, buffer.data(), buffer.size()))
            continue;

        for (size_t i = 0; i + size + 5 <= buffer.size(); ++i)
        {
            // Trampoline tail: [original bytes][E9 rel32 back to hookSite + size]
            if (buffer[i + size] != 0xE9 || std::memcmp(buffer.data() + i, original, size) != 0)
                continue;

            int32_t trampolineRel = 0;
            std::memcpy(&trampolineRel, buffer.data() + i + size + 1, sizeof(trampolineRel));

            const uintptr_t jumpBackFrom = regionBegin + i + size;
            const uintptr_t returnTarget =
                jumpBackFrom + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(trampolineRel));

            if (returnTarget < imageBegin + size || returnTarget >= imageEnd)
                continue;

            const uintptr_t hookSite = returnTarget - size;

            // Second link: the hook site must hold our JMP (E9, NOP padding
            // after it) and that JMP must land in this very region.
            std::vector<uint8_t> patched(size);
            if (!m_memory.ReadBytes(hookSite, patched.data(), size) || patched[0] != 0xE9)
                continue;

            bool padded = true;
            for (size_t b = 5; b < size; ++b)
                padded = padded && patched[b] == 0x90;
            if (!padded)
                continue;

            int32_t hookRel = 0;
            std::memcpy(&hookRel, patched.data() + 1, sizeof(hookRel));
            const uintptr_t jumpTarget =
                hookSite + 5 + static_cast<uintptr_t>(static_cast<intptr_t>(hookRel));

            // The cave body comes first, so the target is at or before the
            // copy of the original bytes.
            if (jumpTarget < regionBegin || jumpTarget > regionBegin + i)
                continue;

            sites.push_back(hookSite);
        }
    }

    std::sort(sites.begin(), sites.end());
    sites.erase(std::unique(sites.begin(), sites.end()), sites.end());
    return sites;
}

std::vector<uintptr_t> OffsetResolver::FindAllPattern(const char* signature) const
{
    std::vector<int> bytes; // -1 = wildcard
    std::string s(signature);
    for (size_t i = 0; i < s.size(); )
    {
        while (i < s.size() && s[i] == ' ') ++i;
        if (i >= s.size()) break;
        if (s[i] == '?')
        {
            bytes.push_back(-1);
            while (i < s.size() && s[i] == '?') ++i;
        }
        else
        {
            bytes.push_back(static_cast<int>(std::strtol(s.c_str() + i, nullptr, 16)));
            i += 2;
        }
    }

    std::vector<uintptr_t> hits;
    const size_t size = bytes.size();
    if (size == 0)
        return hits;

    constexpr size_t kChunk = 0x10000;
    std::vector<uint8_t> buffer(kChunk + size);

    for (const Range& range : CodeSections())
    {
        for (uintptr_t start = range.begin; start < range.end; start += kChunk)
        {
            const size_t want = (std::min)(static_cast<size_t>(range.end - start), kChunk + size);
            if (want < size || !m_memory.ReadBytes(start, buffer.data(), want))
                continue;

            for (size_t i = 0; i + size <= want && i < kChunk; ++i)
            {
                bool match = true;
                for (size_t b = 0; b < size; ++b)
                {
                    if (bytes[b] != -1 && buffer[i + b] != static_cast<uint8_t>(bytes[b]))
                    {
                        match = false;
                        break;
                    }
                }
                if (match)
                    hits.push_back(start + i);
            }
        }
    }
    return hits;
}

std::wstring OffsetResolver::DebugReport() const
{
    const uintptr_t base = m_memory.GetModuleBase();
    std::wstring text = L"--- OffsetResolver ---\n";

    for (const auto& entry : m_entries)
    {
        const wchar_t* how =
            entry.source == Source::Leftover ? L"LEFTOVER" :
            entry.source == Source::Manual ? L"MANUAL" :
            entry.source == Source::Signature ? L"signature" :
            entry.source == Source::Delta ? L"delta" :
            entry.source == Source::Fallback ? L"fallback" :
            entry.source == Source::NarrowScan ? L"SCAN (near)" :
            entry.source == Source::FullScan ? L"SCAN (full)" : L"NOT FOUND";
            

        const wchar_t* note = entry.overrideRejected ? L"  (override rejected)" : L"";

        wchar_t line[224];
        if (entry.isScalar && entry.chosen.first)
            swprintf_s(line, L"%-20s %-12s value %llu%s\n", entry.name.c_str(), how,
                static_cast<unsigned long long>(entry.chosen.first), note);
        else if (entry.chosen.first)
            swprintf_s(line, L"%-20s %-12s RVA 0x%llX%s\n", entry.name.c_str(), how,
                static_cast<unsigned long long>(entry.chosen.first - base), note);
        else
            swprintf_s(line, L"%-20s %-12s%s\n", entry.name.c_str(), how, note);

        text += line;
    }

    if (m_hasDelta)
    {
        wchar_t line[96];
        swprintf_s(line, L"learned data-table shift: %+lld (0x%llX)\n",
            static_cast<long long>(m_delta), static_cast<unsigned long long>(m_delta < 0 ? -m_delta : m_delta));
        text += line;
    }

    for (const auto& note : m_notes)
        text += note + L"\n";
    for (const auto& warning : m_warnings)
        text += L"WARNING: " + warning + L"\n";

    // Ready-to-paste values, so nothing has to be computed by hand.
    std::wstring suggestions;
    for (const auto& entry : m_entries)
    {
        if (entry.source == Source::None) continue;

        if (entry.isScalar)
        {
            if (entry.firstConstant && entry.chosen.first && entry.chosen.first != entry.lastKnown.first)
            {
                wchar_t line[160];
                swprintf_s(line, L"constexpr int32_t %s = %llu;\n", entry.firstConstant,
                    static_cast<unsigned long long>(entry.chosen.first));
                suggestions += line;
            }
            continue;
        }

        auto emit = [&](const wchar_t* constant, uintptr_t address, uintptr_t known) {
            if (!constant || !address || address == known) return;
            wchar_t line[160];
            swprintf_s(line, L"constexpr uintptr_t %s = 0x%llX;\n", constant,
                static_cast<unsigned long long>(address - base));
            suggestions += line;
            };

        emit(entry.firstConstant, entry.chosen.first, entry.lastKnown.first);
        emit(entry.secondConstant, entry.chosen.second, entry.lastKnown.second);
    }

    if (!suggestions.empty())
        text += L"\nPaste into OstrivOffsets.h (only values that differ from the header):\n" + suggestions;

    if (!m_details.empty())
    {
        text += L"\n--- candidates (developer only, sorted by RVA) ---\n";
        for (const auto& block : m_details)
            text += block + L"\n";
    }

    return text;
}