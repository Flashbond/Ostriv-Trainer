#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

#include "InterfaceTypes.h"

class BuildingManager;
class ResourceManager;
class Building;
class JsonManager;
class SelectionTracker;
class CameraController;
class MoneyController;

class InterfaceManager
{
public:
    InterfaceManager(
        BuildingManager& buildingManager,
        ResourceManager& resourceManager,
        JsonManager& jsonManager,
        SelectionTracker* selectionTracker,
        CameraController* cameraController,
        MoneyController* moneyController);

    void FastUpdate();
    void SlowUpdate();

    bool SelectBuilding(uintptr_t address);
    void ClearSelection();

    // locked: reflects the checkbox state at the moment "Set Amount" was
    // clicked. true -> creates/updates a persistent lock for this resource
    // and applies it immediately. false -> clears any existing lock for
    // this resource, then applies the amount once.
    bool SetSelectedResourceAmount(int32_t resourceId, float desiredAmount, bool locked);

    // Removes only the persisted lock — never touches the live amount.
    // Used when the user unchecks a lock checkbox directly (no need to
    // press "Set Amount" to unlock).
    bool ClearSelectedResourceLock(int32_t resourceId);

    bool SetSelectedBuildingCustomName(const std::wstring& customName);

    bool SetCurrentResourceAmount(int32_t resourceId, float desiredAmount, bool locked);
    bool ClearCurrentResourceLock(int32_t resourceId);
    bool SetCurrentBuildingCustomName(const std::wstring& customName);

    void SetTypeFilter(const std::wstring& filter);
    void SetShowOnlyOwnedTypes(bool value);

    bool CenterOnSelectedBuilding();
    bool CenterOnCurrentBuilding();

    const std::vector<std::pair<int32_t, std::wstring>>& GetKnownResources() const;

    bool AddSelectedResource(int32_t resourceId, float amount);
    bool RemoveSelectedResource(int32_t resourceId);

    bool AddCurrentResource(int32_t resourceId, float amount);
    bool RemoveCurrentResource(int32_t resourceId);

    bool SetMoney(const std::wstring& text, bool locked);
    bool ClearMoneyLock();

    // For UI to know which rows in a just-populated inventory should show
    // as checked.
    std::unordered_set<int32_t> GetLockedResourceIds(uintptr_t address) const;

private:
    bool SetCurrentBuilding(uintptr_t address);
    void ClearCurrentBuilding();

    void RefreshBuildingListDisplay();
    void RefreshTypeFilterOptions();

    void BuildBuildingList();
    void BuildBuildingTypes();

    void RefreshMoneyDisplay();
    double m_lastDisplayedMoney = -1.0; // sentinel: no real balance is ever negative, so this always differs on the first call
    bool m_hasDisplayedMoney = false;

    void BuildSelectedInventory();
    void BuildCurrentInventory();

    bool PollInventoryIfPending(uintptr_t address, std::vector<ResourceListItem>& output);
    bool InventoryContentsEqual(const std::vector<ResourceListItem>& a, const std::vector<ResourceListItem>& b) const;

    bool BuildBuildingItem(const Building& building, BuildingListItem& output) const;
    void BuildInventoryItems(const Building& building, std::vector<ResourceListItem>& output) const;

    bool InitializeJson();

    bool RegisterNewBuildings(const std::vector<Building*>& newlyVisible);

    bool SetBuildingCustomName(uintptr_t address, const std::wstring& customName);

    // Applies every persisted resource lock to its live building, if that
    // building is currently tracked and active/not demolishing. Only ever
    // visits the (small) set of actually-locked buildings — never the full
    // collection.
    void ApplyResourceLocks();
    void ApplyMoneyLock();

    bool ApplyAmountToBuilding(Building& building, int32_t resourceId, float amount) const;

private:
    BuildingManager& m_buildingManager;
    ResourceManager& m_resourceManager;
    JsonManager& m_jsonManager;
    SelectionTracker* m_selectionTracker;
    CameraController* m_cameraController;
    MoneyController* m_moneyController;

    bool m_jsonInitialized;
    bool m_showOnlyOwnedTypes;

    std::wstring m_currentTypeFilter;

    uintptr_t m_selectedBuildingAddress;
    std::vector<BuildingListItem> m_buildingList;
    std::vector<std::wstring> m_buildingTypes;
    std::vector<std::wstring> m_lastPushedTypes;
    std::vector<ResourceListItem> m_selectedInventory;

    uintptr_t m_currentBuildingAddress;
    std::vector<ResourceListItem> m_currentInventory;
};