#include "BuildingManager.h"
#include "../Core/RemoteMemory.h"
#include "OstrivOffsets.h"
#include "ResourceManager.h"
#include <algorithm>
#include <execution>
#include <mutex>
#include <numeric>
#include <unordered_set>

namespace
{
    constexpr int kMissingTickThreshold = 3;
    constexpr int kNewBuildingConfirmTicks = 3;
    constexpr int kActiveConfirmTicks = 3;
}

BuildingManager::BuildingManager(RemoteMemory& memory, ResourceManager& resourceManager, uintptr_t moduleBase)
    : m_memory(memory),
    m_resourceManager(resourceManager),
    m_moduleBase(moduleBase),
    m_buildingTable(0),
    m_buildingCount(0),
    m_ready(false),
    m_hasCompletedFirstSync(false)
{
}

bool BuildingManager::ResolveBuildingTable()
{
    m_buildingTable = 0;
    m_buildingCount = 0;
    m_ready = false;

    uintptr_t managerAddress = m_moduleBase + Ostriv::BUILDING_MANAGER_OFFSET;

    uintptr_t tableAddress = 0;
    int32_t count = 0;

    if (!m_memory.Read(managerAddress + Ostriv::BUILDING_MANAGER_ARRAY_OFFSET, tableAddress))
        return false;

    if (!m_memory.Read(managerAddress + Ostriv::BUILDING_MANAGER_COUNT_OFFSET, count))
        return false;

    if (!tableAddress)
        return false;

    if (count <= 0 || count > Ostriv::MAX_BUILDING_ARRAY_COUNT)
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

BuildingManager::SyncResult BuildingManager::RunSlowSync()
{
    SyncResult result;

    if (!ResolveBuildingTable())
        return result;

    result.ok = true;

    const bool isFirstSync = !m_hasCompletedFirstSync;

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

    for (auto& building : m_buildings)
    {
        if (!building) continue;

        bool present = currentSet.count(building->GetAddress()) != 0;

        if (present)
        {
            building->MarkSeenThisTick();

            if (!building->IsListVisible())
                building->MarkPresentTick();

            building->RefreshStatus();
            building->AdvanceActiveStability();

            if (building->GetActiveStabilityTicks() >= kActiveConfirmTicks)
            {
                bool oldConfirmed = building->GetActiveStatus();
                building->PromoteActiveStatus();
                if (building->GetActiveStatus() != oldConfirmed && building->IsListVisible())
                    anyChange = true;
            }

            if (building->IsDemolishing())
                building->MarkMissingThisTick();
        }
        else
        {
            building->MarkMissingThisTick();
        }

        if (!building->IsListVisible() && building->GetPresentTicks() >= kNewBuildingConfirmTicks)
        {
            building->MarkListVisible();
            result.newlyVisible.push_back(building.get());
            anyChange = true;
        }
    }

    m_buildings.erase(
        std::remove_if(
            m_buildings.begin(),
            m_buildings.end(),
            [&anyChange, this](const std::unique_ptr<Building>& building)
            {
                if (!building) return true;

                bool shouldRemove = building->GetMissingTicks() >= kMissingTickThreshold;
                if (shouldRemove)
                {
                    if (building->IsListVisible())
                        anyChange = true;

                    const std::wstring& uid = building->GetUniqueId();
                    if (!uid.empty())
                        m_byUniqueId.erase(uid);
                }

                return shouldRemove;
            }),
        m_buildings.end()
    );

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
            newBuilding->MarkSeenThisTick();
            newBuilding->MarkPresentTick();
            newBuilding->RefreshStatus();
            newBuilding->AdvanceActiveStability();

            if (isFirstSync)
            {
                newBuilding->PromoteActiveStatus();
                newBuilding->MarkListVisible();
                result.newlyVisible.push_back(newBuilding.get());
                anyChange = true;
            }

            if (!newBuilding->GetUniqueId().empty())
                m_byUniqueId[newBuilding->GetUniqueId()] = newBuilding.get();

            m_buildings.push_back(std::move(newBuilding));
        }

        trackedAddresses.insert(address);
    }

    m_hasCompletedFirstSync = true;

    result.changed = anyChange;
    return result;
}

bool BuildingManager::IsValidBuildingAddress(uintptr_t address) const
{
    return address >= Ostriv::MIN_VALID_POINTER && address <= Ostriv::MAX_VALID_POINTER;
}