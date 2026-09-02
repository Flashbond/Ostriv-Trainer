#include "Inventory.h"

#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <map>

#ifdef FindResource
#undef FindResource
#endif

Inventory::Inventory(RemoteMemory& memory, ResourceManager& resourceManager)
    : m_memory(memory), m_resourceManager(resourceManager), m_buildingAddress(0), m_arrayAddress(0), m_count(0) {
}

bool Inventory::Update(uintptr_t buildingAddress) {
    Clear();
    if (buildingAddress == 0) return false;

    m_buildingAddress = buildingAddress;
    return ReadInventoryData();
}

bool Inventory::IsValid() const {
    return m_buildingAddress != 0 && m_arrayAddress != 0 && m_count > 0;
}

uintptr_t Inventory::GetBuildingAddress() const { return m_buildingAddress; }
int Inventory::GetCount() const { return m_count; }
const std::vector<Resource>& Inventory::GetResources() const { return m_resources; }

Resource* Inventory::FindResource(int32_t resourceId) {
    for (auto& resource : m_resources) {
        if (resource.GetId() == resourceId) return &resource;
    }
    return nullptr;
}

const Resource* Inventory::FindResource(int32_t resourceId) const {
    for (const auto& resource : m_resources) {
        if (resource.GetId() == resourceId) return &resource;
    }
    return nullptr;
}

bool Inventory::SetAmount(int32_t resourceId, float desiredAmount) {
    if (m_buildingAddress == 0 || m_arrayAddress == 0) return false;
    if (!std::isfinite(desiredAmount)) return false;
    if (desiredAmount < 0.0f) desiredAmount = 0.0f;

    struct EntryRef { int index; float amount; };
    std::vector<EntryRef> entries;
    float total = 0.0f;

    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i) {
        int32_t currentResourceId = -1;
        float currentAmount = 0.0f;
        uint8_t awaiting = 0;

        if (!ReadEntry(i, currentResourceId, currentAmount, awaiting)) continue;
        if (!IsValidEntry(currentResourceId, currentAmount, awaiting)) continue;
        if (currentResourceId != resourceId) continue;

        entries.push_back({ i, currentAmount });
        total += currentAmount;
    }

    if (entries.empty())
        return false;

    if (desiredAmount >= total)
    {
        // Growing (or matching) the total: pile the entire increase onto
        // the first entry — no removal involved, packing is unaffected.
        float delta = desiredAmount - total;
        uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(entries.front().index) * Ostriv::INVENTORY_ENTRY_SIZE);
        float newAmount = entries.front().amount + delta;

        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, newAmount))
            return false;
    }
    else
    {
        constexpr float kZeroEpsilon = 0.0005f;
        float toRemove = total - desiredAmount;

        // Process from the HIGHEST index down to the lowest: removing an
        // entry shifts every later index down by one, which would
        // invalidate the remaining (lower) indices already queued here if
        // we went the other way. Highest-first means every removal only
        // ever affects indices we've already handled.
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (toRemove <= 0.0f)
                break;

            float consumed = (std::min)(it->amount, toRemove);
            float newAmount = it->amount - consumed;
            toRemove -= consumed;

            if (newAmount <= kZeroEpsilon)
            {
                if (it->index < m_count)
                {
                    if (!RemoveEntryAt(it->index))
                        return false;
                }
                else
                {
                    // Outside the currently-packed region — leftover from
                    // before this fix existed in an older save. Compaction
                    // doesn't apply out here; just clear it in place.
                    uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(it->index) * Ostriv::INVENTORY_ENTRY_SIZE);
                    if (!m_memory.WriteBytes(entryAddress, Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN, sizeof(Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN)))
                        return false;
                }
            }
            else
            {
                uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(it->index) * Ostriv::INVENTORY_ENTRY_SIZE);
                if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, newAmount))
                    return false;
            }
        }
    }

    Resource* resource = FindResource(resourceId);
    if (resource)
        resource->SetAmount(desiredAmount);

    return true;
}

void Inventory::Clear() {
    m_buildingAddress = 0;
    m_arrayAddress = 0;
    m_count = 0;
    m_resources.clear();
}

bool Inventory::ReadInventoryData() {
    int32_t count = 0;
    uintptr_t arrayAddress = 0;

    if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, count)) return false;
    if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_ARRAY_OFFSET, arrayAddress)) return false;

    if (count <= 0 || count > Ostriv::MAX_INVENTORY_ENTRIES || arrayAddress == 0) return false;

    m_count = count;
    m_arrayAddress = arrayAddress;

    // Resource ID -> accumulated amount + first physical slot
    struct Group {
        float amount = 0.0f;
        int firstIndex = -1;
    };

    std::map<int32_t, Group> groups;

    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i) {
        int32_t resourceId = -1;
        float amount = 0.0f;
        uint8_t awaiting = 0;

        if (!ReadEntry(i, resourceId, amount, awaiting)) continue;
        if (!IsValidEntry(resourceId, amount, awaiting)) continue;

        auto& group = groups[resourceId];
        group.amount += amount;
        if (group.firstIndex < 0) group.firstIndex = i;
    }

    for (const auto& [resourceId, group] : groups) {
        if (group.firstIndex < 0) continue;

        std::wstring resourceName;
        if (!m_resourceManager.GetResourceName(resourceId, resourceName)) {
            resourceName = L"Resource_" + std::to_wstring(resourceId);
        }

        m_resources.emplace_back(resourceId, resourceName, group.amount, group.firstIndex);
    }

    return true;
}

bool Inventory::ReadEntry(int index, int32_t& resourceId, float& amount, uint8_t& awaiting) const {
    resourceId = -1;
    amount = 0.0f;
    awaiting = 0;

    // Bound against the array's real physical capacity, not just the
    // (possibly stale/gappy) reported count — a resource removal can
    // leave a genuinely populated entry sitting beyond count.
    if (!IsValid() || index < 0 || index >= static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX)) return false;

    uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(index) * Ostriv::INVENTORY_ENTRY_SIZE);

    if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, resourceId)) return false;
    if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, amount)) return false;
    if (!m_memory.Read(entryAddress + 0x09, awaiting)) return false;

    return true;
}

bool Inventory::IsValidEntry(int32_t resourceId, float amount, uint8_t awaiting) const {
    // resourceId == 0 is the game's own "None" placeholder entry — not a
    // real material, never show it. resourceId < 0 or >= the known table
    // size (Ostriv::RESOURCE_TABLE_COUNT) means we caught this entry mid
    // in-flight write (the game hasn't finished populating it yet) — the
    // bytes we're reading as an id/amount pair aren't meaningful data,
    // just whatever happened to be there at that instant.
    if (resourceId <= 0 || resourceId >= static_cast<int32_t>(Ostriv::RESOURCE_TABLE_COUNT))
        return false;

    if (!std::isfinite(amount) || amount <= 0.0f) return false;

    // awaiting == 0 → delivered/permanent record. != 0 → still in transit, don't count.
    if (awaiting != 0) return false;

    return true;
}

bool Inventory::AddResource(int32_t resourceId, float amount) {
    if (m_buildingAddress == 0) return false;
    if (!std::isfinite(amount) || amount <= 0.0f) return false;

    uintptr_t arrayAddress = m_arrayAddress;

    if (arrayAddress == 0)
    {
        if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_ARRAY_OFFSET, arrayAddress) || arrayAddress == 0)
            return false;

        m_arrayAddress = arrayAddress; // keep the member in sync, SetAmount() below relies on it
    }

    // If this resource type already exists anywhere in the inventory
    // (even split across multiple batch entries), this isn't a new type —
    // grow its existing total via SetAmount() instead of creating a
    // redundant entry and incorrectly incrementing the +0x198 type count.
    float existingTotal = 0.0f;
    bool alreadyPresent = false;

    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i)
    {
        uintptr_t entryAddress = arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

        int32_t currentId = 0;
        float currentAmount = 0.0f;
        uint8_t awaiting = 0;

        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, currentId)) continue;
        if (currentId != resourceId) continue;

        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, currentAmount)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AWAITING_OFFSET, awaiting)) continue;
        if (awaiting != 0) continue;

        alreadyPresent = true;
        existingTotal += currentAmount;
    }

    if (alreadyPresent)
        return SetAmount(resourceId, existingTotal + amount);

    // Genuinely search for the first empty slot across the array's real
    // physical capacity — never just "the slot right after count".
    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i)
    {
        uintptr_t entryAddress = arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

        int32_t existingId = 0;
        float existingAmount = 0.0f;
        int32_t statusBlock = 0;
        uint8_t outgoingFlag = 0;

        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, existingId)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, existingAmount)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_STATUS_BLOCK_OFFSET, statusBlock)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESERVED_OFFSET, outgoingFlag)) continue;

        if (existingId != 0 || existingAmount != 0.0f || statusBlock != 0 || outgoingFlag != 0)
            continue; // not empty — keep looking

        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, resourceId)) return false;
        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, amount)) return false;

        // This genuinely IS a brand-new type — +0x198 goes up by exactly one.
        int32_t newCount = m_count + 1;
        m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, newCount);

        return true;
    }

    return false; // no empty slot found anywhere in the array
}

bool Inventory::RemoveEntryAt(int index)
{
    if (index < 0 || index >= m_count) return false;

    for (int i = index; i < m_count - 1; ++i)
    {
        uintptr_t dst = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);
        uintptr_t src = m_arrayAddress + (static_cast<uintptr_t>(i + 1) * Ostriv::INVENTORY_ENTRY_SIZE);

        uint64_t chunk1 = 0, chunk2 = 0;
        uint32_t chunk3 = 0;

        if (!m_memory.Read(src, chunk1)) return false;
        if (!m_memory.Read(src + 8, chunk2)) return false;
        if (!m_memory.Read(src + 16, chunk3)) return false;

        if (!m_memory.Write(dst, chunk1)) return false;
        if (!m_memory.Write(dst + 8, chunk2)) return false;
        if (!m_memory.Write(dst + 16, chunk3)) return false;
    }

    // The now-vacated last slot goes back to the shared empty template.
    uintptr_t lastAddress = m_arrayAddress + (static_cast<uintptr_t>(m_count - 1) * Ostriv::INVENTORY_ENTRY_SIZE);
    if (!m_memory.WriteBytes(lastAddress, Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN, sizeof(Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN)))
        return false;

    --m_count;
    return m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, m_count);
}

bool Inventory::ClearResource(int32_t resourceId) {
    if (m_buildingAddress == 0 || m_arrayAddress == 0 || m_count <= 0) return false;

    bool removedAny = false;

    // Repeatedly find-and-compact-remove the first matching entry within
    // the packed [0, count) range. Re-scanning from the top after each
    // removal avoids having to track how earlier removals shifted later
    // indices — simple and correct, at the cost of an extra pass or two.
    bool foundOne = true;
    while (foundOne)
    {
        foundOne = false;

        for (int i = 0; i < m_count; ++i)
        {
            uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

            int32_t currentId = 0;
            if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, currentId))
                continue;

            if (currentId != resourceId)
                continue;

            if (!RemoveEntryAt(i))
                return removedAny;

            removedAny = true;
            foundOne = true;
            break; // restart — count and every index after i just shifted
        }
    }

    return removedAny;
}