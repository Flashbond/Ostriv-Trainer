#include "InterfaceManager.h"

#include "../Game/BuildingManager.h"
#include "../Game/Building.h"
#include "../Game/Inventory.h"
#include "../Game/Resource.h"
#include "../Game/ResourceManager.h"
#include "../Game/SelectionTracker.h"
#include "../Game/MoneyController.h"
#include "../Json/JsonManager.h"
#include "../Camera/CameraController.h"
#include "../UI/UI.h"

#include <algorithm>

namespace
{
    // Sentinel resource id for the family-money row — never a real resource,
    // so it can never collide with an actual inventory entry.
    constexpr int32_t kFamilyMoneyResourceId = -100;
}

InterfaceManager::InterfaceManager(
    BuildingManager& buildingManager,
    ResourceManager& resourceManager,
    JsonManager& jsonManager,
    SelectionTracker* selectionTracker,
    CameraController* cameraController,
    MoneyController* moneyController)
    : m_buildingManager(buildingManager),
    m_resourceManager(resourceManager),
    m_jsonManager(jsonManager),
    m_selectionTracker(selectionTracker),
    m_cameraController(cameraController),
    m_moneyController(moneyController),
    m_jsonInitialized(false),
    m_typeFilterInitialized(false),
    m_showOnlyOwnedTypes(true),
    m_selectedBuildingAddress(0),
    m_currentBuildingAddress(0)
{
}

void InterfaceManager::FastUpdate()
{
    if (m_selectionTracker)
    {
        m_selectionTracker->Update();
        SetCurrentBuilding(m_selectionTracker->GetSelectedBuildingAddress());
    }

    ApplyResourceLocks();

    if (PollInventoryIfPending(m_selectedBuildingAddress, m_selectedInventory))
        UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(m_selectedBuildingAddress));

    if (PollInventoryIfPending(m_currentBuildingAddress, m_currentInventory))
        UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(m_currentBuildingAddress));

    ApplyMoneyLock();
    RefreshMoneyDisplay();
}

void InterfaceManager::SlowUpdate()
{
    if (!InitializeJson())
        return;

    BuildingManager::SyncResult result = m_buildingManager.RunSlowSync();

    if (!result.ok)
    {
        UI::SetStatus(L"Lost connection to the process.");
        return;
    }

    if (RegisterNewBuildings(result.newlyVisible))
        m_jsonManager.Save();

    if (result.changed)
    {
        BuildBuildingList();
        RefreshBuildingListDisplay();
    }

    if (!m_typeFilterInitialized)
    {
        RefreshTypeFilterOptions();
        m_typeFilterInitialized = true;
    }
}

void InterfaceManager::ApplyResourceLocks()
{
    for (BuildingRecord* record : m_jsonManager.GetLockedRecords())
    {
        Building* building = m_buildingManager.GetBuildingByUniqueId(record->uniqueId);
        if (!building)
            continue; // not currently tracked live — nothing to do until it reappears

        if (!building->GetActiveStatus() || building->IsDemolishing())
            continue;

        building->RefreshInventory(); // the only per-tick cost this feature adds: one refresh per locked building

        for (const auto& lock : record->resourceLocks)
            ApplyAmountToBuilding(*building, lock.resourceId, lock.amount);
    }
}

void InterfaceManager::ApplyMoneyLock()
{
    if (!m_moneyController || !m_jsonManager.GetMoneyLocked())
        return;

    m_moneyController->SetMoney(m_jsonManager.GetMoneyLockAmount());
}

bool InterfaceManager::ApplyAmountToBuilding(Building& building, int32_t resourceId, float amount) const
{
    if (resourceId == kFamilyMoneyResourceId)
        return building.SetFamilyMoney(amount);

    if (!building.HasResource())
        return false;

    return building.GetInventory().SetAmount(resourceId, amount);
}

std::unordered_set<int32_t> InterfaceManager::GetLockedResourceIds(uintptr_t address) const
{
    std::unordered_set<int32_t> result;
    if (!address)
        return result;

    const Building* building = m_buildingManager.GetBuilding(address);
    if (!building)
        return result;

    const BuildingRecord* record = m_jsonManager.FindBuilding(building->GetUniqueId());
    if (!record)
        return result;

    for (const auto& lock : record->resourceLocks)
        result.insert(lock.resourceId);

    return result;
}

bool InterfaceManager::PollInventoryIfPending(uintptr_t address, std::vector<ResourceListItem>& cache)
{
    if (!address)
        return false;

    Building* building = m_buildingManager.GetBuilding(address);
    if (!building)
        return false;

    if (!building->HasPendingInventory())
        return false;

    building->RefreshInventory();

    std::vector<ResourceListItem> fresh;
    BuildInventoryItems(*building, fresh);

    // `cache` holds whatever we last actually painted for this panel — if
    // nothing changed since then, skip the repaint entirely.
    if (InventoryContentsEqual(fresh, cache))
        return false;

    cache = std::move(fresh);
    return true;
}

void InterfaceManager::SetTypeFilter(const std::wstring& filter)
{
    m_currentTypeFilter = filter;
    RefreshBuildingListDisplay();
}

void InterfaceManager::RefreshBuildingListDisplay()
{
    if (m_currentTypeFilter.empty())
    {
        UI::PopulateBuildingList(m_buildingList);
        return;
    }

    std::vector<BuildingListItem> filtered;
    for (const auto& item : m_buildingList)
        if (item.dictionaryName == m_currentTypeFilter)
            filtered.push_back(item);

    UI::PopulateBuildingList(filtered);
}

bool InterfaceManager::SelectBuilding(uintptr_t address)
{
    if (!address)
    {
        ClearSelection();
        return false;
    }

    Building* building = m_buildingManager.GetBuilding(address);
    if (!building)
    {
        ClearSelection();
        return false;
    }

    m_selectedBuildingAddress = address;
    UI::ClearSelectedPendingChecks();

    building->RefreshInventory();
    BuildSelectedInventory();
    UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(address));

    BuildingListItem item;
    if (BuildBuildingItem(*building, item))
    {
        UI::SetSelectedNameEditText(item.displayName);
        UI::SetSelectedNameEnabled(true);
        UI::SetSelectedCenterEnabled(true);
    }

    return true;
}

void InterfaceManager::ClearSelection()
{
    UI::ClearSelectedPendingChecks();

    m_selectedBuildingAddress = 0;
    m_selectedInventory.clear();

    UI::PopulateSelectedInventory(m_selectedInventory, {});
    UI::SetSelectedNameEditText(L"");
    UI::SetSelectedNameEnabled(false);
    UI::SetSelectedCenterEnabled(false);
}

bool InterfaceManager::SetSelectedResourceAmount(int32_t resourceId, float desiredAmount, bool locked)
{
    if (!m_selectedBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();

    if (locked)
    {
        if (!uniqueId.empty())
        {
            m_jsonManager.SetResourceLock(uniqueId, resourceId, desiredAmount);
            m_jsonManager.Save();
        }
    }
    else if (!uniqueId.empty() && m_jsonManager.ClearResourceLock(uniqueId, resourceId))
    {
        m_jsonManager.Save();
    }

    if (!ApplyAmountToBuilding(*building, resourceId, desiredAmount))
        return false;

    BuildSelectedInventory();
    UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(m_selectedBuildingAddress));
    return true;
}

bool InterfaceManager::ClearSelectedResourceLock(int32_t resourceId)
{
    if (!m_selectedBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();
    if (uniqueId.empty() || !m_jsonManager.ClearResourceLock(uniqueId, resourceId))
        return false;

    m_jsonManager.Save();

    BuildSelectedInventory();
    UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(m_selectedBuildingAddress));
    return true;
}

bool InterfaceManager::SetSelectedBuildingCustomName(const std::wstring& customName)
{
    return SetBuildingCustomName(m_selectedBuildingAddress, customName);
}

bool InterfaceManager::SetCurrentBuilding(uintptr_t address)
{
    bool addressChanged = (address != m_currentBuildingAddress);

    if (!address)
    {
        if (addressChanged)
            ClearCurrentBuilding();
        return false;
    }

    Building* building = m_buildingManager.GetBuilding(address);
    if (!building)
    {
        if (addressChanged)
            ClearCurrentBuilding();
        return false;
    }

    m_currentBuildingAddress = address;

    if (addressChanged)
    {
        UI::ClearCurrentPendingChecks();

        BuildingListItem item;
        if (BuildBuildingItem(*building, item))
        {
            UI::SetCurrentNameEditText(item.displayName);
            UI::SetCurrentNameEnabled(true);
            UI::SetCurrentCenterEnabled(true);
        }

        building->RefreshInventory();
        BuildCurrentInventory();
        UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(address));
    }

    return true;
}

void InterfaceManager::ClearCurrentBuilding()
{
    UI::ClearCurrentPendingChecks();

    m_currentBuildingAddress = 0;
    m_currentInventory.clear();

    UI::PopulateCurrentInventory(m_currentInventory, {});
    UI::SetCurrentNameEditText(L"");
    UI::SetCurrentNameEnabled(false);
    UI::SetCurrentCenterEnabled(false);
}

bool InterfaceManager::SetCurrentResourceAmount(int32_t resourceId, float desiredAmount, bool locked)
{
    if (!m_currentBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();

    if (locked)
    {
        if (!uniqueId.empty())
        {
            m_jsonManager.SetResourceLock(uniqueId, resourceId, desiredAmount);
            m_jsonManager.Save();
        }
    }
    else if (!uniqueId.empty() && m_jsonManager.ClearResourceLock(uniqueId, resourceId))
    {
        m_jsonManager.Save();
    }

    if (!ApplyAmountToBuilding(*building, resourceId, desiredAmount))
        return false;

    BuildCurrentInventory();
    UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(m_currentBuildingAddress));
    return true;
}

bool InterfaceManager::ClearCurrentResourceLock(int32_t resourceId)
{
    if (!m_currentBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();
    if (uniqueId.empty() || !m_jsonManager.ClearResourceLock(uniqueId, resourceId))
        return false;

    m_jsonManager.Save();

    BuildCurrentInventory();
    UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(m_currentBuildingAddress));
    return true;
}

bool InterfaceManager::SetCurrentBuildingCustomName(const std::wstring& customName)
{
    return SetBuildingCustomName(m_currentBuildingAddress, customName);
}

bool InterfaceManager::SetBuildingCustomName(uintptr_t address, const std::wstring& customName)
{
    if (!address)
        return false;

    const Building* building = m_buildingManager.GetBuilding(address);
    if (!building)
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();
    if (uniqueId.empty())
        return false;

    BuildingRecord* record = m_jsonManager.FindBuilding(uniqueId);
    if (!record)
        return false;

    record->customName = customName;

    if (!m_jsonManager.Save())
        return false;

    BuildBuildingList();
    RefreshBuildingListDisplay();

    std::wstring effectiveName = customName.empty() ? building->GetDisplayName() : customName;

    if (address == m_selectedBuildingAddress)
        UI::SetSelectedNameEditText(effectiveName);

    if (address == m_currentBuildingAddress)
        UI::SetCurrentNameEditText(effectiveName);

    return true;
}

void InterfaceManager::BuildBuildingList()
{
    m_buildingList.clear();

    const auto& buildings = m_buildingManager.GetBuildings();
    m_buildingList.reserve(buildings.size());

    for (const auto& building : buildings)
    {
        if (!building || !building->IsListVisible() || !building->GetActiveStatus())
            continue;

        BuildingListItem item;
        if (BuildBuildingItem(*building, item))
            m_buildingList.push_back(std::move(item));
    }
}

void InterfaceManager::BuildBuildingTypes()
{
    if (!m_buildingTypes.empty())
        return;

    m_buildingTypes = Building::GetKnownTypeNames();
}

void InterfaceManager::BuildSelectedInventory()
{
    m_selectedInventory.clear();

    if (!m_selectedBuildingAddress)
        return;

    const Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
    {
        m_selectedBuildingAddress = 0;
        return;
    }

    BuildInventoryItems(*building, m_selectedInventory);
}

void InterfaceManager::BuildCurrentInventory()
{
    m_currentInventory.clear();

    if (!m_currentBuildingAddress)
        return;

    const Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
    {
        m_currentBuildingAddress = 0;
        return;
    }

    BuildInventoryItems(*building, m_currentInventory);
}

bool InterfaceManager::BuildBuildingItem(const Building& building, BuildingListItem& output) const
{
    output.address = building.GetAddress();
    output.dictionaryName = building.GetDictionaryName();
    output.persistentId = building.GetPersistentId();
    output.type = building.GetType();
    output.active = building.GetActiveStatus();
    output.hasResource = building.HasResource();

    const BuildingRecord* record = m_jsonManager.FindBuilding(building.GetUniqueId());

    if (record && !record->customName.empty())
        output.displayName = record->customName;
    else
        output.displayName = building.GetDisplayName();

    return true;
}

void InterfaceManager::BuildInventoryItems(const Building& building, std::vector<ResourceListItem>& output) const
{
    if (building.HasFamilyMoney())
    {
        ResourceListItem item;
        item.id = kFamilyMoneyResourceId;
        item.name = L"Family Money";
        item.amount = building.GetFamilyMoney();
        output.push_back(item);
    }

    if (!building.HasResource())
        return;

    const auto& resources = building.GetInventory().GetResources();
    output.reserve(output.size() + resources.size());

    for (const auto& resource : resources)
    {
        ResourceListItem item;
        item.id = resource.GetId();
        item.name = resource.GetName();
        item.amount = resource.GetAmount();
        output.push_back(std::move(item));
    }
}

bool InterfaceManager::InitializeJson()
{
    if (m_jsonInitialized)
        return true;

    if (!m_jsonManager.Load())
        return false;

    m_showOnlyOwnedTypes = m_jsonManager.GetShowOnlyOwnedTypes();
    UI::SetOwnedOnlyChecked(m_showOnlyOwnedTypes);
    UI::SetMoneyLockChecked(m_jsonManager.GetMoneyLocked());

    m_jsonInitialized = true;
    return true;
}

bool InterfaceManager::RegisterNewBuildings(const std::vector<Building*>& newlyVisible)
{
    bool addedAny = false;

    for (Building* building : newlyVisible)
    {
        if (!building)
            continue;

        const std::wstring& uniqueId = building->GetUniqueId();
        if (uniqueId.empty())
            continue;

        if (m_jsonManager.FindBuilding(uniqueId))
            continue;

        BuildingRecord newRecord;
        newRecord.uniqueId = uniqueId;
        newRecord.dictionaryName = building->GetDictionaryName();
        newRecord.persistentId = building->GetPersistentId();
        newRecord.customName.clear();
        newRecord.active = building->GetActiveStatus();

        if (m_jsonManager.AddBuilding(newRecord))
            addedAny = true;
    }

    return addedAny;
}

void InterfaceManager::SetShowOnlyOwnedTypes(bool value)
{
    if (m_showOnlyOwnedTypes == value)
        return;

    m_showOnlyOwnedTypes = value;
    m_jsonManager.SetShowOnlyOwnedTypes(value);
    m_jsonManager.Save();

    m_lastPushedTypes.clear();
    RefreshTypeFilterOptions();

    m_currentTypeFilter = UI::GetSelectedTypeFilter();
    RefreshBuildingListDisplay();
}

void InterfaceManager::RefreshTypeFilterOptions()
{
    std::vector<std::wstring> desiredTypes;

    if (m_showOnlyOwnedTypes)
    {
        std::unordered_set<std::wstring> owned;
        for (const auto& item : m_buildingList)
            owned.insert(item.dictionaryName);

        desiredTypes.assign(owned.begin(), owned.end());
        std::sort(desiredTypes.begin(), desiredTypes.end());
    }
    else
    {
        BuildBuildingTypes();
        desiredTypes = m_buildingTypes;
    }

    if (desiredTypes != m_lastPushedTypes)
    {
        UI::PopulateTypeFilter(desiredTypes);
        m_lastPushedTypes = desiredTypes;
    }
}

bool InterfaceManager::CenterOnSelectedBuilding()
{
    if (!m_cameraController || !m_selectedBuildingAddress)
        return false;

    const Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
        return false;

    return m_cameraController->CenterOn(building->GetPositionX(), building->GetPositionY());
}

bool InterfaceManager::CenterOnCurrentBuilding()
{
    if (!m_cameraController || !m_currentBuildingAddress)
        return false;

    const Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
        return false;

    return m_cameraController->CenterOn(building->GetPositionX(), building->GetPositionY());
}

void InterfaceManager::RefreshMoneyDisplay()
{
    if (!m_moneyController || !m_moneyController->IsResolved())
        return;

    double money = 0.0;
    if (!m_moneyController->GetMoney(money))
        return;

    wchar_t buffer[64];
    swprintf_s(buffer, L"%.2f", money);
    UI::SetMoneyDisplay(buffer);
}

bool InterfaceManager::SetMoney(const std::wstring& text, bool locked)
{
    if (!m_moneyController)
        return false;

    double value = wcstod(text.c_str(), nullptr);

    m_jsonManager.SetMoneyLock(locked, value);
    m_jsonManager.Save();

    if (!m_moneyController->SetMoney(value))
        return false;

    RefreshMoneyDisplay();
    return true;
}

bool InterfaceManager::ClearMoneyLock()
{
    if (!m_jsonManager.GetMoneyLocked())
        return false;

    m_jsonManager.SetMoneyLock(false, m_jsonManager.GetMoneyLockAmount());
    return m_jsonManager.Save();
}

const std::vector<std::pair<int32_t, std::wstring>>& InterfaceManager::GetKnownResources() const
{
    return m_resourceManager.GetKnownResources();
}

bool InterfaceManager::AddSelectedResource(int32_t resourceId, float amount)
{
    if (!m_selectedBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
        return false;

    if (!building->GetInventory().AddResource(resourceId, amount))
        return false;

    building->RefreshInventory(); // pick up the +0x198 count change together with the new entry
    BuildSelectedInventory();
    UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(m_selectedBuildingAddress));
    return true;
}

bool InterfaceManager::RemoveSelectedResource(int32_t resourceId)
{
    if (!m_selectedBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_selectedBuildingAddress);
    if (!building)
        return false;

    if (!building->GetInventory().ClearResource(resourceId))
        return false;

    // A removed resource no longer exists — an old lock on it would just
    // linger uselessly in the JSON.
    const std::wstring& uniqueId = building->GetUniqueId();
    if (!uniqueId.empty() && m_jsonManager.ClearResourceLock(uniqueId, resourceId))
        m_jsonManager.Save();

    building->RefreshInventory();
    BuildSelectedInventory();
    UI::PopulateSelectedInventory(m_selectedInventory, GetLockedResourceIds(m_selectedBuildingAddress));
    return true;
}

bool InterfaceManager::AddCurrentResource(int32_t resourceId, float amount)
{
    if (!m_currentBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
        return false;

    if (!building->GetInventory().AddResource(resourceId, amount))
        return false;

    building->RefreshInventory();
    BuildCurrentInventory();
    UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(m_currentBuildingAddress));
    return true;
}

bool InterfaceManager::RemoveCurrentResource(int32_t resourceId)
{
    if (!m_currentBuildingAddress)
        return false;

    Building* building = m_buildingManager.GetBuilding(m_currentBuildingAddress);
    if (!building)
        return false;

    if (!building->GetInventory().ClearResource(resourceId))
        return false;

    const std::wstring& uniqueId = building->GetUniqueId();
    if (!uniqueId.empty() && m_jsonManager.ClearResourceLock(uniqueId, resourceId))
        m_jsonManager.Save();

    building->RefreshInventory();
    BuildCurrentInventory();
    UI::PopulateCurrentInventory(m_currentInventory, GetLockedResourceIds(m_currentBuildingAddress));
    return true;
}

bool InterfaceManager::InventoryContentsEqual(const std::vector<ResourceListItem>& a, const std::vector<ResourceListItem>& b) const
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].id != b[i].id || a[i].amount != b[i].amount)
            return false;
    }

    return true;
}