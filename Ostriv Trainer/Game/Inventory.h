#pragma once

#include <cstdint>
#include <vector>

#include "Resource.h"

class RemoteMemory;
class ResourceManager;

class Inventory
{
public:
    Inventory(
        RemoteMemory& memory,
        ResourceManager& resourceManager
    );

    bool Update(uintptr_t buildingAddress);

    bool IsValid() const;

    uintptr_t GetBuildingAddress() const;

    int GetCount() const;

    const std::vector<Resource>& GetResources() const;

    Resource* FindResource(int32_t resourceId);

    const Resource* FindResource(int32_t resourceId) const;

    bool SetAmount(int32_t resourceId, float desiredAmount);

    // Writes a brand-new resource into the first empty slot (index ==
    // current count), then increments the count. Refuses (returns false,
    // writes nothing) if that slot isn't genuinely empty — this defends
    // against a building whose capacity/layout doesn't match our
    // "entries are packed at the front" assumption, without needing to
    // know that building's actual capacity in advance.
    bool AddResource(int32_t resourceId, float amount);

    // Resets EVERY entry matching resourceId back to the game's own
    // "empty slot" template (id=0, amount=0, status block=0, sentinel
    // stays -1.0f) — loops the whole array since batch-type resources can
    // be split across multiple non-contiguous entries. Does NOT shift
    // later entries down or decrement count; this may leave a gap inside
    // the 0..count-1 range. Experimental — verify in-game before relying
    // on it.
    bool ClearResource(int32_t resourceId);
    void Clear();

private:
    bool ReadInventoryData();

    bool ReadEntry(int index, int32_t& resourceId, float& amount, uint8_t& awaiting) const;

    bool IsValidEntry(int32_t resourceId, float amount, uint8_t awaiting) const;

private:
    RemoteMemory& m_memory;
    ResourceManager& m_resourceManager;

    uintptr_t m_buildingAddress;

    uintptr_t m_arrayAddress;

    int m_count;

    std::vector<Resource> m_resources;
};