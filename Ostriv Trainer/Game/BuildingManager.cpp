#include "BuildingManager.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"
#include <algorithm>
#include <execution>
#include <mutex>
#include <numeric>
#include <unordered_set>

BuildingManager::BuildingManager(RemoteMemory& memory, ResourceManager& resourceManager, uintptr_t moduleBase)
    : m_memory(memory),
    m_resourceManager(resourceManager),
    m_moduleBase(moduleBase),
    m_buildingTable(0),
    m_buildingCount(0),
    m_ready(false),
    m_phase(1),
    m_countSamples{ -1, -1, -1 },
    m_tableSamples{ 0, 0, 0 }
{
}

bool BuildingManager::ResolveBuildingTable()
{
    m_buildingTable = 0;
    m_buildingCount = 0;
    m_ready = false;

    uintptr_t managerAddress = m_moduleBase + Ostriv::INGAME_BUILDINGS_TABLE_OFFSET;

    uintptr_t tableAddress = 0;
    int32_t count = 0;

    if (!m_memory.Read(managerAddress + Ostriv::INGAME_BUILDINGS_TABLE_ARRAY_OFFSET, tableAddress))
        return false;

    if (!m_memory.Read(managerAddress + Ostriv::INGAME_BUILDINGS_TABLE_COUNT_OFFSET, count))
        return false;

    if (!tableAddress)
        return false;

    if (count <= 0 || count > Ostriv::INGAME_BUILDINGS_TABLE_MAX_COUNT)
        return false;

    m_buildingTable = tableAddress;
    m_buildingCount = count;
    m_ready = true;

    return true;
}

const std::vector<std::unique_ptr<Building>>& BuildingManager::GetBuildings() const { return m_buildings; }

Building* BuildingManager::GetBuilding(uintptr_t address)
{
    for (auto& building : m_buildings)
        if (building && building->GetAddress() == address)
            return building.get();
    return nullptr;
}

const Building* BuildingManager::GetBuilding(uintptr_t address) const
{
    for (const auto& building : m_buildings)
        if (building && building->GetAddress() == address)
            return building.get();
    return nullptr;
}

Building* BuildingManager::GetBuildingByUniqueId(const std::wstring& uniqueId)
{
    if (uniqueId.empty())
        return nullptr;

    auto it = m_byUniqueId.find(uniqueId);
    return it != m_byUniqueId.end() ? it->second : nullptr;
}

size_t BuildingManager::GetBuildingCount() const { return m_buildings.size(); }
bool BuildingManager::IsReady() const { return m_ready; }

BuildingManager::SyncResult BuildingManager::Tick()
{
    SyncResult result;

    if (m_phase >= 1 && m_phase <= 3)
    {
        if (!ResolveBuildingTable())
        {
            m_phase = 1;
            return result; // ok stays false — caller reports "lost connection"
        }

        int idx = m_phase - 1;
        m_countSamples[idx] = m_buildingCount;
        m_tableSamples[idx] = m_buildingTable;

        result.ok = true;
        ++m_phase;
        return result;
    }

    if (m_phase == 4)
    {
        result.ok = true;

        int32_t agreedCount = -1;
        uintptr_t agreedTable = 0;

        for (int i = 0; i < 3 && agreedCount < 0; ++i)
        {
            int matches = 0;
            for (int j = 0; j < 3; ++j)
                if (m_countSamples[j] == m_countSamples[i])
                    ++matches;

            if (matches >= 2)
            {
                agreedCount = m_countSamples[i];
                agreedTable = m_tableSamples[i];
            }
        }

        if (agreedCount >= 0)
        {
            m_buildingTable = agreedTable;
            m_buildingCount = agreedCount;
            result = RunFullScan();
            result.scanned = true;
        }

        m_phase = 5;
        return result;
    }

    result.ok = true;
    m_phase = 1;
    return result;
}

BuildingManager::SyncResult BuildingManager::RunFullScan()
{
    SyncResult result;
    result.ok = true;

    if (!m_buildingTable || m_buildingCount <= 0)
        return result;

    Building::BuildDictionaryCache(m_memory);

    std::vector<uintptr_t> currentAddresses;
    std::mutex addressesMutex;

    std::vector<int32_t> indices(static_cast<size_t>(m_buildingCount));
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(std::execution::par, indices.begin(), indices.end(), [&](int32_t i) {
        uintptr_t entryAddress = m_buildingTable + static_cast<uintptr_t>(i) * sizeof(uintptr_t);
        uintptr_t buildingAddress = 0;

        if (m_memory.Read(entryAddress, buildingAddress) && IsValidBuildingAddress(buildingAddress))
        {
            std::lock_guard<std::mutex> lock(addressesMutex);
            currentAddresses.push_back(buildingAddress);
        }
        });

    std::unordered_set<uintptr_t> currentSet(currentAddresses.begin(), currentAddresses.end());

    bool anyChange = false;

    for (auto it = m_buildings.begin(); it != m_buildings.end(); )
    {
        Building* building = it->get();

        if (!currentSet.count(building->GetAddress()))
        {
            bool wasShown = building->GetActiveStatus();

            if (!building->GetUniqueId().empty())
                m_byUniqueId.erase(building->GetUniqueId());

            it = m_buildings.erase(it);

            if (wasShown)
                anyChange = true;

            continue;
        }

        bool oldActive = building->GetActiveStatus();
        building->RefreshStatus();
        bool newActive = building->GetActiveStatus();

        if (oldActive != newActive)
            anyChange = true;

        ++it;
    }

    std::unordered_set<uintptr_t> trackedAddresses;
    trackedAddresses.reserve(m_buildings.size());
    for (const auto& b : m_buildings)
        trackedAddresses.insert(b->GetAddress());

    for (uintptr_t address : currentAddresses)
    {
        if (trackedAddresses.count(address))
            continue;

        auto newBuilding = std::make_unique<Building>(m_memory, m_resourceManager);
        if (newBuilding->ResolveIdentity(address))
        {
            newBuilding->RefreshStatus();

            if (!newBuilding->GetUniqueId().empty())
                m_byUniqueId[newBuilding->GetUniqueId()] = newBuilding.get();

            if (newBuilding->GetActiveStatus())
            {
                result.newlyVisible.push_back(newBuilding.get());
                anyChange = true;
            }

            m_buildings.push_back(std::move(newBuilding));
        }

        trackedAddresses.insert(address);
    }

    result.changed = anyChange;
    return result;
}

bool BuildingManager::IsValidBuildingAddress(uintptr_t address) const
{
    return address >= Ostriv::MIN_VALID_POINTER && address <= Ostriv::MAX_VALID_POINTER;
}