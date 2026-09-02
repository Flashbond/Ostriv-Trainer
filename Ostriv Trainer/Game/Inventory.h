#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Resource.h"

class RemoteMemory;
class ResourceManager;

class Inventory
{
public:
    Inventory(RemoteMemory& memory, ResourceManager& resourceManager);

    bool Update(uintptr_t buildingAddress);
    void Clear();

    bool IsValid() const;

    uintptr_t GetBuildingAddress() const;
    int GetCount() const;
    const std::vector<Resource>& GetResources() const;

    Resource* FindResource(int32_t resourceId);
    const Resource* FindResource(int32_t resourceId) const;

    bool SetAmount(int32_t resourceId, float desiredAmount);
    bool AddResource(int32_t resourceId, float amount);
    bool ClearResource(int32_t resourceId);

private:
    bool ReadInventoryData();
    bool ReadEntry(int index, int32_t& resourceId, float& amount, uint8_t& awaiting) const;
    bool IsValidEntry(int32_t resourceId, float amount, uint8_t awaiting) const;

    // Populates m_arrayAddress/m_capacity directly from the building if
    // they aren't already known — used by AddResource/ClearResource/
    // SetAmount so they work correctly even when called without a prior
    // Update() (e.g. a building whose inventory was empty last poll).
    bool EnsureArrayInfo();

    // Shifts every entry after `index` down by one slot and decrements
    // count — matches the game's own removal algorithm exactly
    // (confirmed via decompilation). Leaving a gap instead of compacting
    // would cause real entries further along to fall outside the range
    // the game itself scans for display.
    bool RemoveEntryAt(int index);

private:
    RemoteMemory& m_memory;
    ResourceManager& m_resourceManager;

    uintptr_t m_buildingAddress;
    uintptr_t m_arrayAddress;
    int32_t m_count;
    int32_t m_capacity; // this building's own inventory capacity — entries region is capacity * INVENTORY_ENTRY_SIZE bytes, no padding

    std::vector<Resource> m_resources;
};