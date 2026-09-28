#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class RemoteMemory;

// Absolute addresses inside the target process, resolved once per
// connection. 0 means "not found".
struct ResolvedOffsets
{
    uintptr_t buildingsCountSlot = 0;
    uintptr_t buildingsArraySlot = 0;

    uintptr_t dictionaryCountSlot = 0;
    uintptr_t dictionaryPointerSlot = 0;

    uintptr_t resourceTable = 0;   // address of entry 0
    uintptr_t moneyStateSlot = 0;  // slot holding the pointer to the game-state object

    uintptr_t cameraHook = 0;
    uintptr_t selectionHook = 0;
};

class OffsetResolver
{
public:
    enum class Source { None, Manual, Signature, Delta, Fallback, NarrowScan, FullScan };

    struct Slots
    {
        uintptr_t first = 0;
        uintptr_t second = 0;
    };

    struct Entry
    {
        std::wstring name;
        Source source = Source::None;
        bool overrideRejected = false;
        Slots chosen;
        Slots lastKnown;
        const wchar_t* firstConstant = nullptr;   // OstrivOffsets.h constant holding chosen.first's RVA
        const wchar_t* secondConstant = nullptr;
    };

    explicit OffsetResolver(RemoteMemory& memory);

    ResolvedOffsets Resolve();

    // Items that could not be found by any method.
    std::wstring ProblemSummary() const;

    // Items found, but NOT by signature or last-known value (delta, scan,
    // manual override), plus rejected overrides. Meant for the status bar:
    // something moved, or a hand-typed hint is in play.
    std::wstring AttentionSummary() const;

    // How each item was found, plus ready-to-paste constants for every item
    // whose value differs from OstrivOffsets.h.
    std::wstring DebugReport() const;

    void CollectDiagnostics();

private:
    struct Range
    {
        uintptr_t begin = 0;
        uintptr_t end = 0;
    };

    struct Candidate
    {
        Slots slots;
        int32_t count = 0;
    };

    using Validator = std::function<bool(const Slots&)>;
    // around == 0: scan every writable section. Otherwise only
    // [around - radius, around + radius] inside them.
    using Scanner = std::function<Slots(uintptr_t around, uintptr_t radius)>;

    struct Item
    {
        const wchar_t* name = L"";
        const wchar_t* firstConstant = nullptr;
        const wchar_t* secondConstant = nullptr;
        Slots manual;
        Slots fromSignature;
        Slots lastKnown;
        Validator isValid;
        Scanner scan;
        bool useDelta = false;   // false for code addresses (hooks)
    };

    uintptr_t Find(const char* signature) const;
    uintptr_t RipTarget(uintptr_t instruction) const;

    Slots Choose(const Item& item);

    const std::vector<Range>& WritableSections() const;
    void LoadSections() const;
    const std::vector<Range>& CodeSections() const;

    std::vector<uintptr_t> FindAllBytes(const uint8_t* needle, size_t size) const;
    bool BodyTouchesInventory(uintptr_t function) const;
    bool ScanWritableData(size_t window, uintptr_t around, uintptr_t radius,
        const std::function<bool(uintptr_t, const uint8_t*)>& visit) const;
    std::vector<Candidate> CollectCountPointerPairs(const Validator& isValid, uintptr_t around, uintptr_t radius) const;
    Slots ScanCountPointerPairs(const Validator& isValid, uintptr_t around, uintptr_t radius);
    std::vector<uintptr_t> CollectResourceTables() const;

    std::wstring FormatList(const std::wstring& title,
        std::vector<std::pair<uintptr_t, std::wstring>> rows, uintptr_t chosenAddress) const;
    void AddNote(const std::wstring& note);
    uintptr_t ScanResourceTable(uintptr_t around, uintptr_t radius) const;

private:
    RemoteMemory& m_memory;
    std::vector<Entry> m_entries;

    mutable std::vector<Range> m_sections;
    mutable bool m_sectionsLoaded = false;

    mutable std::vector<Range> m_codeSections;

    std::vector<std::wstring> m_notes;      // informational, report only
    std::vector<std::wstring> m_warnings;   // report + status bar

    bool m_hasDelta = false;
    intptr_t m_delta = 0;

    std::vector<std::wstring> m_details;   // candidate lists, report only
    ResolvedOffsets m_result;              // what Resolve() picked, for marking "in use"
};