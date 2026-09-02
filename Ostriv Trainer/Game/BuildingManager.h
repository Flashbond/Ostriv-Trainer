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
        bool scanned = false; // true only on the cycle's one full address scan (phase 4, when 2-of-3 count samples agreed)
        std::vector<Building*> newlyVisible;
    };

    BuildingManager(RemoteMemory& memory, ResourceManager& resourceManager, uintptr_t moduleBase);

    bool ResolveBuildingTable();

    // Call once per second. Cycles through a 5-second, 5-phase sync cycle:
    //   Phase 1-3: one quick reading each of the raw table's reported
    //              count — occasionally jitters even while paused, so
    //              three quick samples let a single bad one get outvoted.
    //   Phase 4:   whichever count at least two of the three readings
    //              agreed on is used for the cycle's one full address
    //              scan. Presence is binary — a building either showed up
    //              this scan or it didn't. Active status (Child kind
    //              reads its parent) is a single fresh reading, trusted
    //              immediately. No 2-of-3 agreement → this cycle is
    //              skipped entirely.
    //   Phase 5:   idle.
    SyncResult Tick();

    const std::vector<std::unique_ptr<Building>>& GetBuildings() const;

    Building* GetBuilding(uintptr_t address);
    const Building* GetBuilding(uintptr_t address) const;
    Building* GetBuildingByUniqueId(const std::wstring& uniqueId);

    size_t GetBuildingCount() const;

    bool IsReady() const;

private:
    SyncResult RunFullScan();
    bool IsValidBuildingAddress(uintptr_t address) const;

private:
    RemoteMemory& m_memory;
    ResourceManager& m_resourceManager;

    uintptr_t m_moduleBase;

    uintptr_t m_buildingTable;
    int32_t m_buildingCount;

    bool m_ready;

    int m_phase; // 1..5, cycles
    int32_t m_countSamples[3];
    uintptr_t m_tableSamples[3];

    std::vector<std::unique_ptr<Building>> m_buildings;
    std::unordered_map<std::wstring, Building*> m_byUniqueId;
};