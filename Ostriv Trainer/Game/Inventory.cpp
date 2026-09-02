#include "Inventory.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#ifdef FindResource
#undef FindResource
#endif

Inventory::Inventory(RemoteMemory& memory, ResourceManager& resourceManager)
    : m_memory(memory), m_resourceManager(resourceManager), m_buildingAddress(0), m_arrayAddress(0), m_count(0), m_capacity(0) {
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

bool Inventory::EnsureArrayInfo()
{
    if (m_arrayAddress != 0 && m_capacity > 0)
        return true;

    uintptr_t arrayAddress = 0;
    int32_t capacity = 0;

    if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_ARRAY_OFFSET, arrayAddress) || arrayAddress == 0)
        return false;

    if (!m_memory.Read(m_buildingAddress + Ostriv::BUILDING_INVENTORY_SIZE_OFFSET, capacity) ||
        capacity <= 0 || capacity > Ostriv::INVENTORY_CAPACITY_SANITY_MAX)
        return false;

    m_arrayAddress = arrayAddress;
    m_capacity = capacity;
    return true;
}

bool Inventory::SetAmount(int32_t resourceId, float desiredAmount) {
    if (m_buildingAddress == 0) return false;
    if (!EnsureArrayInfo()) return false;
    if (!std::isfinite(desiredAmount)) return false;
    if (desiredAmount < 0.0f) desiredAmount = 0.0f;

    struct EntryRef { int index; float amount; };
    std::vector<EntryRef> entries;
    float total = 0.0f;

    for (int i = 0; i < m_capacity; ++i) {
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
                    uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(it->index) * Ostriv::INVENTORY_ENTRY_SIZE);
                    uint8_t emptyPattern[Ostriv::INVENTORY_ENTRY_SIZE] = {
                        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
                        0x00,0x00,0x80,0xBF, 0x00,0x00,0x00,0x00
                    };
                    if (!m_memory.WriteBytes(entryAddress, emptyPattern, sizeof(emptyPattern)))
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

bool Inventory::AddResource(int32_t resourceId, float amount) {
    if (m_buildingAddress == 0) return false;
    if (!std::isfinite(amount) || amount <= 0.0f) return false;
    if (!EnsureArrayInfo()) return false;

    float existingTotal = 0.0f;
    bool alreadyPresent = false;

    for (int i = 0; i < m_capacity; ++i)
    {
        uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

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

    for (int i = 0; i < m_capacity; ++i)
    {
        uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(i) * Ostriv::INVENTORY_ENTRY_SIZE);

        int32_t existingId = 0;
        float existingAmount = 0.0f;
        int32_t statusBlock = 0;
        uint8_t outgoingFlag = 0;

        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, existingId)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, existingAmount)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_STATUS_BLOCK_OFFSET, statusBlock)) continue;
        if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESERVED_OFFSET, outgoingFlag)) continue;

        if (existingId != 0 || existingAmount != 0.0f || statusBlock != 0 || outgoingFlag != 0)
            continue;

        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, resourceId)) return false;
        if (!m_memory.Write(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, amount)) return false;

        int32_t newCount = m_count + 1;
        m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, newCount);

        return true;
    }

    return false;
}

bool Inventory::ClearResource(int32_t resourceId) {
    if (m_buildingAddress == 0) return false;
    if (!EnsureArrayInfo()) return false;
    if (m_count <= 0) return false;

    bool removedAny = false;
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
            break;
        }
    }

    return removedAny;
}

void Inventory::Clear() {
    m_buildingAddress = 0;
    m_arrayAddress = 0;
    m_count = 0;
    m_capacity = 0;
    m_resources.clear();
}

bool Inventory::ReadInventoryData() {
    int32_t count = 0;
    uintptr_t arrayAddress = 0;
    int32_t capacity = 0;

    if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, count)) return false;
    if (!m_memory.Read(m_buildingAddress + Ostriv::INVENTORY_ARRAY_OFFSET, arrayAddress)) return false;
    if (!m_memory.Read(m_buildingAddress + Ostriv::BUILDING_INVENTORY_SIZE_OFFSET, capacity)) return false;

    if (capacity <= 0 || capacity > Ostriv::INVENTORY_CAPACITY_SANITY_MAX) return false;
    if (count < 0 || count > capacity || arrayAddress == 0) return false;

    m_count = count;
    m_arrayAddress = arrayAddress;
    m_capacity = capacity;

    struct Group {
        float amount = 0.0f;
        int firstIndex = -1;
    };

    std::map<int32_t, Group> groups;

    for (int i = 0; i < m_capacity; ++i) {
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

    if (m_buildingAddress == 0 || m_arrayAddress == 0 || index < 0 || index >= m_capacity) return false;

    uintptr_t entryAddress = m_arrayAddress + (static_cast<uintptr_t>(index) * Ostriv::INVENTORY_ENTRY_SIZE);

    if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_RESOURCE_ID_OFFSET, resourceId)) return false;
    if (!m_memory.Read(entryAddress + Ostriv::INVENTORY_AMOUNT_OFFSET, amount)) return false;
    if (!m_memory.Read(entryAddress + 0x09, awaiting)) return false;

    return true;
}

bool Inventory::IsValidEntry(int32_t resourceId, float amount, uint8_t awaiting) const {
    if (resourceId <= 0 || resourceId >= static_cast<int32_t>(Ostriv::RESOURCE_TABLE_COUNT))
        return false;

    if (!std::isfinite(amount) || amount <= 0.0f) return false;

    if (awaiting != 0) return false;

    return true;
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

    uintptr_t lastAddress = m_arrayAddress + (static_cast<uintptr_t>(m_count - 1) * Ostriv::INVENTORY_ENTRY_SIZE);
    uint8_t emptyPattern[Ostriv::INVENTORY_ENTRY_SIZE] = {
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x00,0x00,0x80,0xBF, 0x00,0x00,0x00,0x00
    };
    if (!m_memory.WriteBytes(lastAddress, emptyPattern, sizeof(emptyPattern)))
        return false;

    --m_count;
    return m_memory.Write(m_buildingAddress + Ostriv::INVENTORY_COUNT_OFFSET, m_count);
}