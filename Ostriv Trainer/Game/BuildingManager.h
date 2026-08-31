#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

#include "Building.h"

class RemoteMemory;
class ResourceManager;

class BuildingManager
{
public:
    struct SyncResult
    {
        bool ok = false;
        bool changed = false;
        std::vector<Building*> newlyVisible;
    };

    BuildingManager(RemoteMemory& memory, ResourceManager& resourceManager, uintptr_t moduleBase);

    bool ResolveBuildingTable();
    SyncResult RunSlowSync();

    const std::vector<std::unique_ptr<Building>>& GetBuildings() const;

    Building* GetBuilding(uintptr_t address);
    const Building* GetBuilding(uintptr_t address) const;

    // O(1) — for the resource-lock scanner, which needs to find a live
    // Building by its permanent uniqueId (addresses are not stable across
    // sessions, uniqueId is).
    Building* GetBuildingByUniqueId(const std::wstring& uniqueId);

    size_t GetBuildingCount() const;

    bool IsReady() const;

private:
    bool IsValidBuildingAddress(uintptr_t address) const;

private:
    RemoteMemory& m_memory;
    ResourceManager& m_resourceManager;

    uintptr_t m_moduleBase;

    uintptr_t m_buildingTable;
    int32_t m_buildingCount;

    bool m_ready;
    bool m_hasCompletedFirstSync;

    std::vector<std::unique_ptr<Building>> m_buildings;
    std::unordered_map<std::wstring, Building*> m_byUniqueId;
};