#include "Inventory.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"
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
    if (!IsValid() || !std::isfinite(desiredAmount)) return false;
    if (desiredAmount < 0.0f) desiredAmount = 0.0f;

    Resource* resource = FindResource(resourceId);
    if (!resource) return false;

    int sourceIndex = resource->GetSourceIndex();
    if (sourceIndex < 0 || sourceIndex >= m_count) return false;

    uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(sourceIndex) * Ostriv::INVENTORY_ENTRY_SIZE);
    float oldAmount = 0.0f;

    if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, oldAmount)) return false;

    float remaining = desiredAmount - oldAmount;
    if (remaining < 0.0f) remaining = 0.0f;

    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i) {
        if (i == sourceIndex) continue;

        int32_t currentResourceId = -1;
        float currentAmount = 0.0f;
        uint8_t awaiting = 0;

        if (!ReadEntry(i, currentResourceId, currentAmount, awaiting)) continue;
        if (!IsValidEntry(currentResourceId, currentAmount, awaiting)) continue;
        if (currentResourceId != resourceId) continue;

        float newAmount = 0.0f;
        if (remaining > 0.0f) {
            float consumed = (std::min)(currentAmount, remaining);
            newAmount = currentAmount - consumed;
            remaining -= consumed;
        }
        else {
            newAmount = currentAmount;
        }

        uintptr_t currentEntryAddr = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE) + Ostriv::INVENTORY_AMOUNT_OFFSET;
        if (!m_memory.Write(currentEntryAddr, newAmount)) return false;
    }

    if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, desiredAmount)) return false;

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
        // The building's own inventory was empty (+0x198 == 0) the last
        // time it was refreshed, so RefreshInventory()/Update() never
        // bothered resolving the array pointer — read it directly here
        // instead of requiring a non-empty inventory first.
        if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_ARRAY_OFFSET, arrayAddress) || arrayAddress == 0)
            return false;
    }

    // Genuinely search for the first empty slot across the array's real
    // physical capacity — never just "the slot right after count".
    // ClearResource() can leave a hole anywhere in that range, and reusing
    // it (instead of always growing past the end) is what keeps this from
    // ever bloating the inventory.
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

        // The whole 4-byte status block must be zero — not just the
        // single awaiting-flag byte inside it — otherwise this slot may
        // be silently reserved for an incoming delivery even though it
        // "looks" empty at a glance.
        if (existingId != 0 || existingAmount != 0.0f || statusBlock != 0 || outgoingFlag != 0)
            continue; // not empty — keep looking

        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, resourceId)) return false;
        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, amount)) return false;

        // Unlike removal, the game does NOT pick up a new entry on its
        // own — confirmed by testing. We always have to bump the count
        // ourselves.
        int32_t newCount = m_count + 1;
        m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, newCount);

        return true;
    }

    return false; // no empty slot found anywhere in the array
}

bool Inventory::ClearResource(int32_t resourceId) {
    if (m_buildingAddress == 0 || m_arrayAddress == 0 || m_count <= 0) return false;

    int clearedCount = 0;

    for (int i = 0; i < static_cast<int>(Ostriv::INVENTORY_MAX_SCAN_INDEX); ++i) {
        uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

        int32_t currentId = 0;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, currentId))
            continue;

        if (currentId != resourceId)
            continue;

        if (m_memory.WriteBytes(entryAddress, Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN, sizeof(Ostriv::INVENTORY_EMPTY_ENTRY_PATTERN)))
            ++clearedCount;
    }

    if (clearedCount == 0)
        return false;

    // Unlike additions, the game does NOT update INVENTORY_COUNT_OFFSET on
    // its own when entries are cleared out from under it — we always have
    // to do it ourselves.
    int32_t newCount = m_count - clearedCount;
    if (newCount < 0) newCount = 0;

    m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, newCount);

    return true;
}