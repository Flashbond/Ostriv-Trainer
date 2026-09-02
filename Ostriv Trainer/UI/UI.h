#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <utility>

#include "../Interface/InterfaceTypes.h"

// Pure UI layer. Creates and refreshes Win32 controls from the plain data
// structs InterfaceManager produces. Deliberately has no dependency on
// RemoteMemory, Building, or any backend class.
namespace UI
{
    enum ControlId : int
    {
        IDC_STATUS_LABEL = 100,
        IDC_CONNECT_BUTTON,
        IDC_ALWAYS_ON_TOP_CHECKBOX,

        IDC_MONEY_LOCK_CHECKBOX,
        IDC_NEW_MONEY_EDIT,
        IDC_SET_MONEY_BUTTON,

        IDC_TYPE_FILTER,
        IDC_OWNED_ONLY_CHECKBOX,
        IDC_BUILDING_LIST,

        IDC_SELECTED_NAME_EDIT,
        IDC_SELECTED_NAME_BUTTON,

        IDC_SELECTED_INVENTORY_LIST,
        IDC_SELECTED_AMOUNT_EDIT,
        IDC_SELECTED_SET_AMOUNT_BUTTON,
        IDC_SELECTED_ADD_BUTTON,
        IDC_SELECTED_REMOVE_BUTTON,
        IDC_SELECTED_SET_BUTTON, // "Center Building"

        IDC_CURRENT_NAME_EDIT,
        IDC_CURRENT_NAME_BUTTON,

        IDC_CURRENT_INVENTORY_LIST,
        IDC_CURRENT_AMOUNT_EDIT,
        IDC_CURRENT_SET_AMOUNT_BUTTON,
        IDC_CURRENT_ADD_BUTTON,
        IDC_CURRENT_REMOVE_BUTTON,
        IDC_CURRENT_SET_BUTTON // "Center Building"
    };
    
    enum class InventoryPanel { Selected, Current };

    void CreateControls(HWND parent, HINSTANCE instance);
    void Layout(HWND parent, int clientWidth, int clientHeight);

    void SetStatus(const std::wstring& text);

    void SetMoneyDisplay(const std::wstring& text);
    void SetNewMoneyEditText(const std::wstring& text);
    std::wstring GetNewMoneyEditText();
    void SetMoneyControlsEnabled(bool enabled);

    void SetMoneyLockChecked(bool checked);
    bool GetMoneyLockChecked();

    void SetAlwaysOnTopChecked(bool checked);
    bool GetAlwaysOnTopChecked();
    void SetConnectButtonState(bool connected);

    void PopulateTypeFilter(const std::vector<std::wstring>& types);
    std::wstring GetSelectedTypeFilter(); // "" means "no filter" / "(All types)"

    void SetTypeFilterToAll();

    void ClearTypeFilter();
    void SetTypeFilterEnabled(bool enabled);

    void SetOwnedOnlyChecked(bool checked);
    bool GetOwnedOnlyChecked();
    void SetOwnedOnlyEnabled(bool enabled);

    void PopulateBuildingList(const std::vector<BuildingListItem>& buildings);
    uintptr_t GetSelectedBuildingAddress();

    void SetSelectedNameEditText(const std::wstring& text);
    std::wstring GetSelectedNameEditText();
    void SetSelectedNameEnabled(bool enabled);

    // Enables/disables BOTH "Set Amount" and "Center Building" for this panel.
    void SetSelectedCenterEnabled(bool enabled);
    void SetSelectedAmountControlsEnabled(bool enabled);
    void PopulateSelectedInventory(const std::vector<ResourceListItem>& resources, const std::unordered_set<int32_t>& lockedResourceIds);
    bool GetSelectedInventoryRow(int32_t& resourceId, std::wstring& amountEditText, bool& checked);

    // Right panel: the building currently selected in-game.
    void SetCurrentNameEditText(const std::wstring& text);
    std::wstring GetCurrentNameEditText();
    void SetCurrentNameEnabled(bool enabled);

    void SetCurrentCenterEnabled(bool enabled);
    void SetCurrentAmountControlsEnabled(bool enabled);
    void PopulateCurrentInventory(const std::vector<ResourceListItem>& resources, const std::unordered_set<int32_t>& lockedResourceIds);
    bool GetCurrentInventoryRow(int32_t& resourceId, std::wstring& amountEditText, bool& checked);

    // Call from WndProc's WM_NOTIFY handler, before any other processing.
    // Returns true only for a genuine user click that just unchecked a lock
    // checkbox (never for PopulateSelectedInventory/PopulateCurrentInventory's
    // own programmatic updates) — outPanel/outResourceId identify the row.
    bool HandleInventoryCheckboxNotification(LPARAM notifyLParam, InventoryPanel& outPanel, int32_t& outResourceId);

    void ResetOnDisconnect();

    // Opens a small popup with a resource dropdown + amount box. Blocks the
    // calling thread until the user presses Add or Cancel (other windows'
    // timers keep firing normally in the meantime — the parent is only
    // disabled, not frozen). Returns true only if "Add" was pressed with a
    // valid selection and a positive amount.
    bool ShowAddResourceDialog(HWND parent,
        const std::vector<std::pair<int32_t, std::wstring>>& knownResources,
        int32_t& outResourceId, float& outAmount);

    // Call when a building selection changes — clears any checkbox the
    // user checked but never confirmed with "Set Amount" for that panel.
    void ClearSelectedPendingChecks();
    void ClearCurrentPendingChecks();
}