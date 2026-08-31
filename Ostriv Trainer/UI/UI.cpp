#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "UI.h"

#include <CommCtrl.h>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include <Uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

namespace UI
{
    namespace
    {
        HWND g_status = nullptr;
        HWND g_connectButton = nullptr;

        HWND g_moneyLabel = nullptr;
        HWND g_moneyLockCheckbox = nullptr;
        HWND g_newMoneyLabel = nullptr;
        HWND g_newMoneyEdit = nullptr;
        HWND g_setMoneyButton = nullptr;

        HWND g_typeFilter = nullptr;
        HWND g_ownedOnlyCheckbox = nullptr;
        HWND g_buildingList = nullptr;

        HWND g_selectedNameEdit = nullptr;
        HWND g_selectedNameButton = nullptr;

        HWND g_selectedInventoryList = nullptr;
        HWND g_selectedAmountEdit = nullptr;
        HWND g_selectedSetAmountButton = nullptr;
        HWND g_selectedSetButton = nullptr; // "Center Building"

        HWND g_currentNameEdit = nullptr;
        HWND g_currentNameButton = nullptr;

        HWND g_currentInventoryList = nullptr;
        HWND g_currentAmountEdit = nullptr;
        HWND g_currentSetAmountButton = nullptr;
        HWND g_currentSetButton = nullptr; // "Center Building"

        HWND g_selectedAddButton = nullptr;
        HWND g_selectedRemoveButton = nullptr;
        HWND g_currentAddButton = nullptr;
        HWND g_currentRemoveButton = nullptr;

        HFONT g_font = nullptr;

        // Suppresses HandleInventoryCheckboxNotification() while
        // PopulateSelectedInventory()/PopulateCurrentInventory() are setting
        // checkbox states programmatically — without this, our own repaint
        // would be mistaken for the user manually unchecking a lock.
        bool g_suppressCheckboxNotifications = false;
        std::unordered_set<int32_t> g_selectedPendingChecked;
        std::unordered_set<int32_t> g_currentPendingChecked;

        void AddListViewColumn(HWND list, int index, const wchar_t* text, int width)
        {
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH;
            column.pszText = const_cast<wchar_t*>(text);
            column.cx = width;
            ListView_InsertColumn(list, index, &column);
        }

        HWND CreateInventoryListView(HWND parent, int id, HINSTANCE instance)
        {
            HWND list = CreateWindowExW(
                0, WC_LISTVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                0, 0, 10, 10,
                parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            
            ListView_SetExtendedListViewStyle(list,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);

            // The checkbox always binds to subitem index 0 — there's no way
            // to attach it to a different subitem. To make it APPEAR as the
            // third (rightmost) column anyway, subitem 0 is defined as
            // "Keep up" but visually reordered to the end; subitems 1/2
            // (Resource/Amount) are reordered to appear first/second.
            AddListViewColumn(list, 0, L"Keep up", 200);
            AddListViewColumn(list, 1, L"Resource", 130);
            AddListViewColumn(list, 2, L"Amount", 80);

            int order[3] = { 1, 2, 0 }; // visual position -> underlying column index
            ListView_SetColumnOrderArray(list, 3, order);

            return list;
        }

        int CALLBACK CompareBuildingListItems(LPARAM lParam1, LPARAM lParam2, LPARAM lParamSort)
        {
            auto* items = reinterpret_cast<const std::unordered_map<uintptr_t, const BuildingListItem*>*>(lParamSort);

            auto it1 = items->find(static_cast<uintptr_t>(lParam1));
            auto it2 = items->find(static_cast<uintptr_t>(lParam2));

            if (it1 == items->end() || it2 == items->end())
                return 0;

            const BuildingListItem& a = *it1->second;
            const BuildingListItem& b = *it2->second;

            if (a.dictionaryName != b.dictionaryName)
                return a.dictionaryName < b.dictionaryName ? -1 : 1;

            if (a.displayName != b.displayName)
                return a.displayName < b.displayName ? -1 : 1;

            return 0;
        }

        void PopulateInventoryList(HWND list, const std::vector<ResourceListItem>& resources,
            const std::unordered_set<int32_t>& lockedResourceIds)
        {
            g_suppressCheckboxNotifications = true;

            SendMessageW(list, WM_SETREDRAW, FALSE, 0);

            int previousTopIndex = ListView_GetTopIndex(list);
            int previousSelectedId = -1;

            int selIndex = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (selIndex >= 0)
            {
                LVITEMW lvItem{};
                lvItem.mask = LVIF_PARAM;
                lvItem.iItem = selIndex;
                ListView_GetItem(list, &lvItem);
                previousSelectedId = static_cast<int>(lvItem.lParam);
            }

            ListView_DeleteAllItems(list);

            int newSelectIndex = -1;

            static const wchar_t* const kKeepUpHint = L"(Click to set amount to keep)";

            for (size_t i = 0; i < resources.size(); ++i)
            {
                const auto& item = resources[i];

                LVITEMW lvItem{};
                lvItem.mask = LVIF_TEXT | LVIF_PARAM;
                lvItem.iItem = static_cast<int>(i);
                lvItem.pszText = const_cast<wchar_t*>(kKeepUpHint); // subitem 0 — where the checkbox lives
                lvItem.lParam = static_cast<LPARAM>(item.id);

                int index = ListView_InsertItem(list, &lvItem);

                ListView_SetItemText(list, index, 1, const_cast<wchar_t*>(item.name.c_str()));

                wchar_t amountText[64];
                swprintf_s(amountText, L"%.2f", item.amount);
                ListView_SetItemText(list, index, 2, amountText);

                // Checked when either genuinely locked (JSON) or checked
                // but not yet confirmed with "Set Amount" (pending).
                bool isChecked = lockedResourceIds.count(item.id) != 0;
                ListView_SetCheckState(list, index, isChecked);

                if (item.id == previousSelectedId)
                    newSelectIndex = index;
            }

            if (newSelectIndex >= 0)
            {
                ListView_SetItemState(list, newSelectIndex,
                    LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }

            int itemCount = ListView_GetItemCount(list);
            int restoreTop = (previousTopIndex >= 0 && previousTopIndex < itemCount) ? previousTopIndex : 0;
            ListView_EnsureVisible(list, restoreTop, FALSE);

            SendMessageW(list, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(list, nullptr, FALSE);

            g_suppressCheckboxNotifications = false;
        }

        bool GetInventoryRow(HWND list, HWND amountEdit, int32_t& resourceId, std::wstring& amountEditText, bool& checked)
        {
            int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (index < 0)
            {
                resourceId = -1;
                checked = false;
                return false;
            }

            LVITEMW lvItem{};
            lvItem.mask = LVIF_PARAM;
            lvItem.iItem = index;
            ListView_GetItem(list, &lvItem);
            resourceId = static_cast<int32_t>(lvItem.lParam);

            checked = ListView_GetCheckState(list, index) != 0;

            wchar_t buffer[64] = {};
            GetWindowTextW(amountEdit, buffer, 63);
            amountEditText = buffer;

            return true;
        }
    }

    void CreateControls(HWND parent, HINSTANCE instance)
    {
        g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        g_status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS_LABEL)), instance, nullptr);

        g_connectButton = CreateWindowW(L"BUTTON", L"Connect", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONNECT_BUTTON)), instance, nullptr);

        g_moneyLabel = CreateWindowW(L"STATIC", L"Money: ", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, nullptr, instance, nullptr);

        g_moneyLockCheckbox = CreateWindowW(L"BUTTON", L"Lock", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MONEY_LOCK_CHECKBOX)), instance, nullptr);

        g_newMoneyLabel = CreateWindowW(L"STATIC", L"New Money:", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, nullptr, instance, nullptr);

        g_newMoneyEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NEW_MONEY_EDIT)), instance, nullptr);

        g_setMoneyButton = CreateWindowW(L"BUTTON", L"Set Money", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SET_MONEY_BUTTON)), instance, nullptr);

        // --- Left column: all buildings ---
        g_typeFilter = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TYPE_FILTER)), instance, nullptr);

        g_ownedOnlyCheckbox = CreateWindowW(L"BUTTON", L"Show only buildings I have",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OWNED_ONLY_CHECKBOX)), instance, nullptr);

        SendMessageW(g_ownedOnlyCheckbox, BM_SETCHECK, BST_CHECKED, 0);

        g_buildingList = CreateWindowExW(
            0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BUILDING_LIST)), instance, nullptr);

        ListView_SetExtendedListViewStyle(g_buildingList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        AddListViewColumn(g_buildingList, 0, L"Name", 220);
        AddListViewColumn(g_buildingList, 1, L"Type", 140);
        AddListViewColumn(g_buildingList, 2, L"Active", 60);

        g_selectedNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_NAME_EDIT)), instance, nullptr);

        g_selectedNameButton = CreateWindowW(L"BUTTON", L"Set Name", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_NAME_BUTTON)), instance, nullptr);

        g_selectedInventoryList = CreateInventoryListView(parent, IDC_SELECTED_INVENTORY_LIST, instance);

        g_selectedAmountEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_AMOUNT_EDIT)), instance, nullptr);

        g_selectedSetAmountButton = CreateWindowW(L"BUTTON", L"Set Amount", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_SET_AMOUNT_BUTTON)), instance, nullptr);

        g_selectedAddButton = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_ADD_BUTTON)), instance, nullptr);

        g_selectedRemoveButton = CreateWindowW(L"BUTTON", L"-", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_REMOVE_BUTTON)), instance, nullptr);

        g_selectedSetButton = CreateWindowW(L"BUTTON", L"Center Building", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_SET_BUTTON)), instance, nullptr);

        // --- Right column: building currently selected in-game ---
        g_currentNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_NAME_EDIT)), instance, nullptr);

        g_currentNameButton = CreateWindowW(L"BUTTON", L"Set Name", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_NAME_BUTTON)), instance, nullptr);

        g_currentInventoryList = CreateInventoryListView(parent, IDC_CURRENT_INVENTORY_LIST, instance);

        g_currentAmountEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_AMOUNT_EDIT)), instance, nullptr);

        g_currentSetAmountButton = CreateWindowW(L"BUTTON", L"Set Amount", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_SET_AMOUNT_BUTTON)), instance, nullptr);

        g_currentAddButton = CreateWindowW(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_ADD_BUTTON)), instance, nullptr);

        g_currentRemoveButton = CreateWindowW(L"BUTTON", L"-", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_REMOVE_BUTTON)), instance, nullptr);

        g_currentSetButton = CreateWindowW(L"BUTTON", L"Center Building", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_SET_BUTTON)), instance, nullptr);

        SetSelectedNameEnabled(false);
        SetCurrentNameEnabled(false);

        SetSelectedCenterEnabled(false);
        SetCurrentCenterEnabled(false);

        SetMoneyControlsEnabled(false);

        EnumChildWindows(parent, [](HWND child, LPARAM) -> BOOL {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            return TRUE;
            }, 0);
    }

    void Layout(HWND parent, int clientWidth, int clientHeight)
    {
        const int margin = 10;
        const int statusHeight = 24;
        const int filterClosedHeight = 24;
        const int filterDropdownHeight = 200;
        const int editHeight = 24;
        const int buttonWidth = 80;
        const int rowGap = 6;

        MoveWindow(g_status, margin, margin, clientWidth - margin * 3 - 100, statusHeight, TRUE);
        MoveWindow(g_connectButton, clientWidth - margin - 100, margin, 100, statusHeight, TRUE);

        // --- Money row ---
        int moneyRowY = margin * 2 + statusHeight;
        int moneyLabelWidth = 130;
        int moneyLockCheckboxWidth = 55;
        int newMoneyLabelWidth = 85;
        int newMoneyEditWidth = 140;
        int setMoneyButtonWidth = 100;

        int moneyX = margin;
        MoveWindow(g_moneyLabel, moneyX, moneyRowY + 1, moneyLabelWidth, editHeight, TRUE);
        moneyX += moneyLabelWidth + rowGap;
        MoveWindow(g_moneyLockCheckbox, moneyX, moneyRowY - 1, moneyLockCheckboxWidth, editHeight, TRUE);
        moneyX += moneyLockCheckboxWidth + rowGap;
        MoveWindow(g_newMoneyLabel, moneyX, moneyRowY + 1, newMoneyLabelWidth, editHeight, TRUE);
        moneyX += newMoneyLabelWidth + rowGap;
        MoveWindow(g_newMoneyEdit, moneyX, moneyRowY - 1, newMoneyEditWidth, editHeight, TRUE);
        moneyX += newMoneyEditWidth + rowGap;
        MoveWindow(g_setMoneyButton, moneyX, moneyRowY - 1, setMoneyButtonWidth, editHeight, TRUE);

        int columnTop = moneyRowY + editHeight + margin;
        int columnHeight = clientHeight - columnTop - margin;
        int columnWidth = (clientWidth - margin * 3) / 2;

        // --- Left column ---
        int leftX = margin;
        int y = columnTop;

        const int checkboxWidth = 190;
        int filterComboWidth = columnWidth - checkboxWidth - rowGap;

        MoveWindow(g_typeFilter, leftX, y, filterComboWidth, filterClosedHeight + filterDropdownHeight, TRUE);
        MoveWindow(g_ownedOnlyCheckbox, leftX + filterComboWidth + rowGap, y, checkboxWidth, filterClosedHeight, TRUE);
        y += filterClosedHeight + rowGap;

        int leftRemaining = columnHeight - (y - columnTop) - editHeight - rowGap - editHeight - rowGap;
        int buildingListHeight = leftRemaining / 2;
        int selectedInventoryHeight = leftRemaining - buildingListHeight;

        MoveWindow(g_buildingList, leftX, y, columnWidth, buildingListHeight, TRUE);
        y += buildingListHeight + rowGap;

        MoveWindow(g_selectedNameEdit, leftX, y, columnWidth - buttonWidth - rowGap, editHeight, TRUE);
        MoveWindow(g_selectedNameButton, leftX + columnWidth - buttonWidth, y, buttonWidth, editHeight, TRUE);
        y += editHeight + rowGap;

        MoveWindow(g_selectedInventoryList, leftX, y, columnWidth, selectedInventoryHeight, TRUE);
        y += selectedInventoryHeight + rowGap;

        // Amount / Set Amount / Center Building, all on one row.
        const int amountEditWidth = 80;
        const int setAmountButtonWidth = 90;
        const int centerButtonWidth = 150;

        const int plusMinusWidth = 32;

        MoveWindow(g_selectedAmountEdit, leftX, y, amountEditWidth, editHeight, TRUE);
        MoveWindow(g_selectedSetAmountButton, leftX + amountEditWidth + rowGap, y, setAmountButtonWidth, editHeight, TRUE);

        int selectedPlusX = leftX + amountEditWidth + rowGap + setAmountButtonWidth + rowGap;
        MoveWindow(g_selectedAddButton, selectedPlusX, y, plusMinusWidth, editHeight, TRUE);
        MoveWindow(g_selectedRemoveButton, selectedPlusX + plusMinusWidth + rowGap, y, plusMinusWidth, editHeight, TRUE);

        MoveWindow(g_selectedSetButton, leftX + columnWidth - centerButtonWidth, y, centerButtonWidth, editHeight, TRUE);

        // --- Right column ---
        int rightX = leftX + columnWidth + margin;
        y = columnTop;

        MoveWindow(g_currentNameEdit, rightX, y, columnWidth - buttonWidth - rowGap, editHeight, TRUE);
        MoveWindow(g_currentNameButton, rightX + columnWidth - buttonWidth, y, buttonWidth, editHeight, TRUE);
        y += editHeight + rowGap;

        int currentInventoryHeight = columnHeight - (y - columnTop) - editHeight - rowGap;
        MoveWindow(g_currentInventoryList, rightX, y, columnWidth, currentInventoryHeight, TRUE);
        y += currentInventoryHeight + rowGap;

        MoveWindow(g_currentAmountEdit, rightX, y, amountEditWidth, editHeight, TRUE);
        MoveWindow(g_currentSetAmountButton, rightX + amountEditWidth + rowGap, y, setAmountButtonWidth, editHeight, TRUE);

        int currentPlusX = rightX + amountEditWidth + rowGap + setAmountButtonWidth + rowGap;
        MoveWindow(g_currentAddButton, currentPlusX, y, plusMinusWidth, editHeight, TRUE);
        MoveWindow(g_currentRemoveButton, currentPlusX + plusMinusWidth + rowGap, y, plusMinusWidth, editHeight, TRUE);

        MoveWindow(g_currentSetButton, rightX + columnWidth - centerButtonWidth, y, centerButtonWidth, editHeight, TRUE);
    }

    void SetStatus(const std::wstring& text)
    {
        SetWindowTextW(g_status, text.c_str());
    }

    void SetMoneyDisplay(const std::wstring& text)
    {
        SetWindowTextW(g_moneyLabel, (L"Money: " + text).c_str());
    }

    void SetNewMoneyEditText(const std::wstring& text)
    {
        SetWindowTextW(g_newMoneyEdit, text.c_str());
    }

    std::wstring GetNewMoneyEditText()
    {
        wchar_t buffer[64] = {};
        GetWindowTextW(g_newMoneyEdit, buffer, 63);
        return buffer;
    }

    void SetMoneyControlsEnabled(bool enabled)
    {
        EnableWindow(g_newMoneyEdit, enabled);
        EnableWindow(g_setMoneyButton, enabled);
        EnableWindow(g_moneyLockCheckbox, enabled);
    }

    void SetMoneyLockChecked(bool checked)
    {
        SendMessageW(g_moneyLockCheckbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetMoneyLockChecked()
    {
        return SendMessageW(g_moneyLockCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void PopulateTypeFilter(const std::vector<std::wstring>& types)
    {
        wchar_t currentText[256] = {};
        int currentSel = static_cast<int>(SendMessageW(g_typeFilter, CB_GETCURSEL, 0, 0));
        if (currentSel >= 0)
            SendMessageW(g_typeFilter, CB_GETLBTEXT, currentSel, reinterpret_cast<LPARAM>(currentText));

        SendMessageW(g_typeFilter, CB_RESETCONTENT, 0, 0);
        SendMessageW(g_typeFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(All types)"));

        for (const auto& type : types)
            SendMessageW(g_typeFilter, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(type.c_str()));

        int restoreIndex = static_cast<int>(SendMessageW(g_typeFilter, CB_FINDSTRINGEXACT,
            static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(currentText)));

        SendMessageW(g_typeFilter, CB_SETCURSEL, restoreIndex >= 0 ? restoreIndex : 0, 0);
    }

    std::wstring GetSelectedTypeFilter()
    {
        int sel = static_cast<int>(SendMessageW(g_typeFilter, CB_GETCURSEL, 0, 0));
        if (sel <= 0)
            return L"";

        wchar_t text[256] = {};
        SendMessageW(g_typeFilter, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(text));
        return text;
    }

    void SetTypeFilterToAll()
    {
        SendMessageW(g_typeFilter, CB_SETCURSEL, 0, 0);
    }

    void SetOwnedOnlyChecked(bool checked)
    {
        SendMessageW(g_ownedOnlyCheckbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetOwnedOnlyChecked()
    {
        return SendMessageW(g_ownedOnlyCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void PopulateBuildingList(const std::vector<BuildingListItem>& buildings)
    {
        std::unordered_map<uintptr_t, const BuildingListItem*> desired;
        desired.reserve(buildings.size());
        for (const auto& item : buildings)
            desired[item.address] = &item;

        SendMessageW(g_buildingList, WM_SETREDRAW, FALSE, 0);

        std::unordered_set<uintptr_t> stillPresent;

        for (int i = ListView_GetItemCount(g_buildingList) - 1; i >= 0; --i)
        {
            LVITEMW lvItem{};
            lvItem.mask = LVIF_PARAM;
            lvItem.iItem = i;
            ListView_GetItem(g_buildingList, &lvItem);

            uintptr_t address = static_cast<uintptr_t>(lvItem.lParam);

            auto it = desired.find(address);
            if (it == desired.end())
            {
                ListView_DeleteItem(g_buildingList, i);
                continue;
            }

            const BuildingListItem& item = *it->second;
            wchar_t buffer[256];

            ListView_GetItemText(g_buildingList, i, 0, buffer, 256);
            if (item.displayName != buffer)
                ListView_SetItemText(g_buildingList, i, 0, const_cast<wchar_t*>(item.displayName.c_str()));

            ListView_GetItemText(g_buildingList, i, 1, buffer, 256);
            if (item.dictionaryName != buffer)
                ListView_SetItemText(g_buildingList, i, 1, const_cast<wchar_t*>(item.dictionaryName.c_str()));

            const wchar_t* activeText = item.active ? L"Yes" : L"No";
            ListView_GetItemText(g_buildingList, i, 2, buffer, 256);
            if (wcscmp(activeText, buffer) != 0)
                ListView_SetItemText(g_buildingList, i, 2, const_cast<wchar_t*>(activeText));

            stillPresent.insert(address);
        }

        for (const auto& item : buildings)
        {
            if (stillPresent.count(item.address))
                continue;

            LVITEMW lvItem{};
            lvItem.mask = LVIF_TEXT | LVIF_PARAM;
            lvItem.iItem = ListView_GetItemCount(g_buildingList);
            lvItem.pszText = const_cast<wchar_t*>(item.displayName.c_str());
            lvItem.lParam = static_cast<LPARAM>(item.address);

            int index = ListView_InsertItem(g_buildingList, &lvItem);
            ListView_SetItemText(g_buildingList, index, 1, const_cast<wchar_t*>(item.dictionaryName.c_str()));
            ListView_SetItemText(g_buildingList, index, 2, const_cast<wchar_t*>(item.active ? L"Yes" : L"No"));
        }

        ListView_SortItems(g_buildingList, CompareBuildingListItems, reinterpret_cast<LPARAM>(&desired));

        SendMessageW(g_buildingList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_buildingList, nullptr, FALSE);
    }

    uintptr_t GetSelectedBuildingAddress()
    {
        int index = ListView_GetNextItem(g_buildingList, -1, LVNI_SELECTED);
        if (index < 0)
            return 0;

        LVITEMW lvItem{};
        lvItem.mask = LVIF_PARAM;
        lvItem.iItem = index;
        ListView_GetItem(g_buildingList, &lvItem);

        return static_cast<uintptr_t>(lvItem.lParam);
    }

    void SetSelectedNameEditText(const std::wstring& text)
    {
        SetWindowTextW(g_selectedNameEdit, text.c_str());
    }

    std::wstring GetSelectedNameEditText()
    {
        wchar_t buffer[256] = {};
        GetWindowTextW(g_selectedNameEdit, buffer, 255);
        return buffer;
    }

    void SetSelectedNameEnabled(bool enabled)
    {
        EnableWindow(g_selectedNameEdit, enabled);
        EnableWindow(g_selectedNameButton, enabled);
    }

    void SetSelectedCenterEnabled(bool enabled)
    {
        EnableWindow(g_selectedSetButton, enabled);
        EnableWindow(g_selectedSetAmountButton, enabled);
        EnableWindow(g_selectedAddButton, enabled);
        EnableWindow(g_selectedRemoveButton, enabled);
    }

    void PopulateSelectedInventory(const std::vector<ResourceListItem>& resources,
        const std::unordered_set<int32_t>& lockedResourceIds)
    {
        std::unordered_set<int32_t> checkedIds = lockedResourceIds;
        checkedIds.insert(g_selectedPendingChecked.begin(), g_selectedPendingChecked.end());
        PopulateInventoryList(g_selectedInventoryList, resources, checkedIds);
    }

    void ClearSelectedPendingChecks()
    {
        g_selectedPendingChecked.clear();
    }

    bool GetSelectedInventoryRow(int32_t& resourceId, std::wstring& amountEditText, bool& checked)
    {
        return GetInventoryRow(g_selectedInventoryList, g_selectedAmountEdit, resourceId, amountEditText, checked);
    }

    void SetCurrentNameEditText(const std::wstring& text)
    {
        SetWindowTextW(g_currentNameEdit, text.c_str());
    }

    std::wstring GetCurrentNameEditText()
    {
        wchar_t buffer[256] = {};
        GetWindowTextW(g_currentNameEdit, buffer, 255);
        return buffer;
    }

    void SetCurrentNameEnabled(bool enabled)
    {
        EnableWindow(g_currentNameEdit, enabled);
        EnableWindow(g_currentNameButton, enabled);
    }

    void SetCurrentCenterEnabled(bool enabled)
    {
        EnableWindow(g_currentSetButton, enabled);
        EnableWindow(g_currentSetAmountButton, enabled);
        EnableWindow(g_currentAddButton, enabled);
        EnableWindow(g_currentRemoveButton, enabled);
    }

    void PopulateCurrentInventory(const std::vector<ResourceListItem>& resources,
        const std::unordered_set<int32_t>& lockedResourceIds)
    {
        std::unordered_set<int32_t> checkedIds = lockedResourceIds;
        checkedIds.insert(g_currentPendingChecked.begin(), g_currentPendingChecked.end());
        PopulateInventoryList(g_currentInventoryList, resources, checkedIds);
    }

    void ClearCurrentPendingChecks()
    {
        g_currentPendingChecked.clear();
    }

    bool GetCurrentInventoryRow(int32_t& resourceId, std::wstring& amountEditText, bool& checked)
    {
        return GetInventoryRow(g_currentInventoryList, g_currentAmountEdit, resourceId, amountEditText, checked);
    }

    bool HandleInventoryCheckboxNotification(LPARAM notifyLParam, InventoryPanel& outPanel, int32_t& outResourceId)
    {
        // Guards BOTH the checkbox-lock logic below AND the amount-autofill
        // side effect — without this, PopulateInventoryList()'s own
        // selection-restore would overwrite whatever the user is mid-typing
        // in the amount box on every repaint.
        if (g_suppressCheckboxNotifications)
            return false;

        auto* header = reinterpret_cast<LPNMHDR>(notifyLParam);
        if (header->code != LVN_ITEMCHANGED)
            return false;

        HWND list = nullptr;
        HWND amountEdit = nullptr;
        InventoryPanel panel;

        if (header->hwndFrom == g_selectedInventoryList)
        {
            list = g_selectedInventoryList;
            amountEdit = g_selectedAmountEdit;
            panel = InventoryPanel::Selected;
        }
        else if (header->hwndFrom == g_currentInventoryList)
        {
            list = g_currentInventoryList;
            amountEdit = g_currentAmountEdit;
            panel = InventoryPanel::Current;
        }
        else
        {
            return false;
        }

        auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(notifyLParam);
        if (!(nmlv->uChanged & LVIF_STATE))
            return false;

        // A row was just selected (plain click, or a checkbox click that
        // also selects it below) — always fill the amount box with that
        // row's current value. Subitem 2 = Amount in visual order.
        bool justSelected = !(nmlv->uOldState & LVIS_SELECTED) && (nmlv->uNewState & LVIS_SELECTED);
        if (justSelected)
        {
            wchar_t amountText[64] = {};
            ListView_GetItemText(list, nmlv->iItem, 2, amountText, 64);
            SetWindowTextW(amountEdit, amountText);
        }

        UINT oldCheck = (nmlv->uOldState & LVIS_STATEIMAGEMASK) >> 12;
        UINT newCheck = (nmlv->uNewState & LVIS_STATEIMAGEMASK) >> 12;

        if (oldCheck == newCheck)
            return false; // not a checkbox transition

        // Clicking the checkbox itself doesn't select the row by default —
        // do it ourselves, and fill the amount box too (covers the case
        // where the row was already selected and "justSelected" above
        // never fired).
        ListView_SetItemState(list, nmlv->iItem, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

        wchar_t amountText[64] = {};
        ListView_GetItemText(list, nmlv->iItem, 2, amountText, 64);
        SetWindowTextW(amountEdit, amountText);

        LVITEMW lvItem{};
        lvItem.mask = LVIF_PARAM;
        lvItem.iItem = nmlv->iItem;
        ListView_GetItem(list, &lvItem);
        int32_t resourceId = static_cast<int32_t>(lvItem.lParam);

        auto& pending = (panel == InventoryPanel::Selected) ? g_selectedPendingChecked : g_currentPendingChecked;

        // Win32 state-image indices: 1 = unchecked, 2 = checked.
        if (oldCheck == 1 && newCheck == 2)
        {
            pending.insert(resourceId);
            return false;
        }

        if (oldCheck == 2 && newCheck == 1)
        {
            pending.erase(resourceId);
            outPanel = panel;
            outResourceId = resourceId;
            return true;
        }

        return false;
    }
    namespace
    {
        struct AddResourceDialogState
        {
            HWND combo = nullptr;
            HWND amountEdit = nullptr;
            bool confirmed = false;
            int32_t resourceId = -1;
            float amount = 0.0f;
        };

        LRESULT CALLBACK AddResourceDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            auto* state = reinterpret_cast<AddResourceDialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

            switch (msg)
            {
            case WM_CREATE:
            {
                auto* cs = reinterpret_cast<LPCREATESTRUCTW>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return 0;
            }

            case WM_COMMAND:
            {
                int id = LOWORD(wParam);
                int notification = HIWORD(wParam);

                if (id == 1 && notification == BN_CLICKED) // Add
                {
                    int sel = static_cast<int>(SendMessageW(state->combo, CB_GETCURSEL, 0, 0));
                    if (sel >= 0)
                    {
                        state->resourceId = static_cast<int32_t>(SendMessageW(state->combo, CB_GETITEMDATA, sel, 0));

                        wchar_t buffer[64] = {};
                        GetWindowTextW(state->amountEdit, buffer, 63);
                        state->amount = wcstof(buffer, nullptr);

                        if (state->resourceId >= 0 && state->amount > 0.0f)
                        {
                            state->confirmed = true;
                            DestroyWindow(hwnd);
                        }
                    }
                    return 0;
                }

                if (id == 2 && notification == BN_CLICKED) // Cancel
                {
                    DestroyWindow(hwnd);
                    return 0;
                }

                return 0;
            }

            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            }

            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    bool ShowAddResourceDialog(HWND parent,
        const std::vector<std::pair<int32_t, std::wstring>>& knownResources,
        int32_t& outResourceId, float& outAmount)
    {
        static bool classRegistered = false;
        const wchar_t* className = L"OstrivAddResourceDialog";

        if (!classRegistered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = AddResourceDialogProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = className;
            wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
            classRegistered = true;
        }

        AddResourceDialogState state;

        HWND dlg = CreateWindowExW(
            WS_EX_DLGMODALFRAME, className, L"Add Resource",
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, 320, 180,
            parent, nullptr, GetModuleHandleW(nullptr), &state);

        if (!dlg)
            return false;

        RECT parentRect{};
        GetWindowRect(parent, &parentRect);
        RECT dlgRect{};
        GetWindowRect(dlg, &dlgRect);
        int dlgWidth = dlgRect.right - dlgRect.left;
        int dlgHeight = dlgRect.bottom - dlgRect.top;
        int x = parentRect.left + ((parentRect.right - parentRect.left) - dlgWidth) / 2;
        int y = parentRect.top + ((parentRect.bottom - parentRect.top) - dlgHeight) / 2;
        SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        HFONT font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        CreateWindowW(L"STATIC", L"Resource:", WS_CHILD | WS_VISIBLE, 15, 18, 80, 20, dlg, nullptr, nullptr, nullptr);

        state.combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            100, 15, 190, 200, dlg, nullptr, nullptr, nullptr);

        for (const auto& entry : knownResources)
        {
            int index = static_cast<int>(SendMessageW(state.combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.second.c_str())));
            SendMessageW(state.combo, CB_SETITEMDATA, index, static_cast<LPARAM>(entry.first));
        }
        if (!knownResources.empty())
            SendMessageW(state.combo, CB_SETCURSEL, 0, 0);

        CreateWindowW(L"STATIC", L"Amount:", WS_CHILD | WS_VISIBLE, 15, 55, 80, 20, dlg, nullptr, nullptr, nullptr);

        state.amountEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            100, 53, 190, 22, dlg, nullptr, nullptr, nullptr);

        CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE, 100, 100, 90, 28, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE, 200, 100, 90, 28, dlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(2)), nullptr, nullptr);

        EnumChildWindows(dlg, [](HWND child, LPARAM lp) -> BOOL {
            SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(lp), TRUE);
            return TRUE;
            }, reinterpret_cast<LPARAM>(font));

        EnableWindow(parent, FALSE);
        ShowWindow(dlg, SW_SHOW);

        MSG msg{};
        while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0))
        {
            if (!IsDialogMessageW(dlg, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
        DeleteObject(font);

        if (state.confirmed)
        {
            outResourceId = state.resourceId;
            outAmount = state.amount;
            return true;
        }

        return false;
    }
}