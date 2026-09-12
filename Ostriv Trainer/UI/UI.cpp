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
#include <cmath>
#include <cstdlib>
#include <cwchar>

#include <Uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

namespace UI
{
    namespace
    {
        HWND g_status = nullptr;
        HWND g_connectButton = nullptr;
        HWND g_alwaysOnTop = nullptr;

        HWND g_moneyLabel = nullptr;
        HWND g_moneyLockCheckbox = nullptr;
        HWND g_newMoneyLabel = nullptr;
        HWND g_newMoneyEdit = nullptr;
        HWND g_setMoneyButton = nullptr;

        HWND g_typeFilterLabel = nullptr;
        HWND g_typeFilter = nullptr;
        HWND g_ownedOnlyCheckbox = nullptr;
        HWND g_buildingList = nullptr;

        HWND g_selectedNameEdit = nullptr;
        HWND g_selectedNameButton = nullptr;

        HWND g_selectedInventoryList = nullptr;
        HWND g_selectedAmountEdit = nullptr;
        HWND g_selectedSetAmountButton = nullptr;
        HWND g_selectedSetButton = nullptr; // "Center Building"

        HWND g_currentBuilding = nullptr;
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
                LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);

            // The checkbox always binds to subitem index 0 — there's no way
            // to attach it to a different subitem. To make it APPEAR as the
            // third (rightmost) column anyway, subitem 0 is defined as
            // "Keep up" but visually reordered to the end; subitems 1/2
            // (Resource/Amount) are reordered to appear first/second.
            AddListViewColumn(list, 0, L"Keep up", 220);
            AddListViewColumn(list, 1, L"Resource", 155);
            AddListViewColumn(list, 2, L"Amount", 100);

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

        LRESULT CALLBACK NumericEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
            UINT_PTR subclassId, DWORD_PTR)
        {
            switch (msg)
            {
            case WM_CHAR:
            {
                wchar_t ch = static_cast<wchar_t>(wParam);

                if (ch < 0x20)
                {
                    // Control character (Backspace, etc.) — let the default
                    // edit control handling perform the deletion itself, then
                    // immediately restore "0.00" if that just emptied the box.
                    LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

                    wchar_t buffer[64] = {};
                    GetWindowTextW(hwnd, buffer, 63);
                    if (buffer[0] == L'\0')
                    {
                        SetWindowTextW(hwnd, L"0.00");
                        SendMessageW(hwnd, EM_SETSEL, 0, -1); // select-all, so the next keystroke overwrites it
                    }

                    return result;
                }

                bool isDigit = (ch >= L'0' && ch <= L'9');
                bool isDot = (ch == L'.');

                if (!isDigit && !isDot)
                    return 0; // reject anything else outright — letters, '-', spaces, symbols

                if (isDot)
                {
                    wchar_t buffer[64] = {};
                    GetWindowTextW(hwnd, buffer, 63);
                    if (wcschr(buffer, L'.') != nullptr)
                        return 0; // already has a decimal point — reject a second one
                }

                break; // valid digit/first dot — let the default edit control processing insert it
            }

            case WM_KEYDOWN:
            {
                // The Delete key does NOT generate WM_CHAR — it has to be
                // caught separately to cover "select all, press Delete"
                // clearing the box the same way Backspace does above.
                if (wParam == VK_DELETE)
                {
                    LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);

                    wchar_t buffer[64] = {};
                    GetWindowTextW(hwnd, buffer, 63);
                    if (buffer[0] == L'\0')
                    {
                        SetWindowTextW(hwnd, L"0.00");
                        SendMessageW(hwnd, EM_SETSEL, 0, -1);
                    }

                    return result;
                }
                break;
            }

            case WM_PASTE:
            {
                if (!OpenClipboard(hwnd))
                    return 0;

                HANDLE data = GetClipboardData(CF_UNICODETEXT);
                bool valid = data != nullptr;

                if (valid)
                {
                    wchar_t* text = static_cast<wchar_t*>(GlobalLock(data));
                    valid = (text != nullptr) && (*text != L'\0');

                    if (valid)
                    {
                        bool seenDot = false;
                        for (const wchar_t* p = text; *p; ++p)
                        {
                            if (*p == L'.')
                            {
                                if (seenDot) { valid = false; break; }
                                seenDot = true;
                            }
                            else if (*p < L'0' || *p > L'9')
                            {
                                valid = false;
                                break;
                            }
                        }
                    }

                    if (text)
                        GlobalUnlock(data);
                }

                CloseClipboard();

                if (!valid)
                    return 0;

                break;
            }

            case WM_KILLFOCUS:
            {
                wchar_t buffer[64] = {};
                GetWindowTextW(hwnd, buffer, 63);

                double value = wcstod(buffer, nullptr);
                if (!std::isfinite(value) || value < 0.0)
                    value = 0.0;

                wchar_t formatted[64];
                swprintf_s(formatted, L"%.2f", value);
                SetWindowTextW(hwnd, formatted);

                break;
            }

            case WM_NCDESTROY:
                RemoveWindowSubclass(hwnd, NumericEditSubclassProc, subclassId);
                break;
            }

            return DefSubclassProc(hwnd, msg, wParam, lParam);
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

        g_alwaysOnTop = CreateWindowW(L"BUTTON", L"Always On Top", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ALWAYS_ON_TOP_CHECKBOX)), instance, nullptr);

        g_moneyLabel = CreateWindowW(L"STATIC", L"Money: ", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, nullptr, instance, nullptr);

        g_newMoneyEdit = CreateWindowW(L"EDIT", L"0.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NEW_MONEY_EDIT)), instance, nullptr);
        SetWindowSubclass(g_newMoneyEdit, NumericEditSubclassProc, 3, 0);

        g_setMoneyButton = CreateWindowW(L"BUTTON", L"Set Money", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SET_MONEY_BUTTON)), instance, nullptr);

        g_moneyLockCheckbox = CreateWindowW(L"BUTTON", L"Lock (Click Set Money)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MONEY_LOCK_CHECKBOX)), instance, nullptr);

        // --- Left column: all buildings ---
        g_typeFilterLabel = CreateWindowW(L"STATIC", L"Type filter:", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, nullptr, instance, nullptr);

        g_typeFilter = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_TYPE_FILTER)), instance, nullptr);

        g_ownedOnlyCheckbox = CreateWindowW(L"BUTTON", L"Show only building types I have",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_OWNED_ONLY_CHECKBOX)), instance, nullptr);

        SendMessageW(g_ownedOnlyCheckbox, BM_SETCHECK, BST_CHECKED, 0);

        g_buildingList = CreateWindowExW(
            0, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_BUILDING_LIST)), instance, nullptr);

        ListView_SetExtendedListViewStyle(g_buildingList, LVS_EX_FULLROWSELECT);
        AddListViewColumn(g_buildingList, 0, L"Name", 320);
        AddListViewColumn(g_buildingList, 1, L"Type", 155);

        g_currentBuilding = CreateWindowW(L"STATIC", L"Active building:", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, nullptr, instance, nullptr);

        g_selectedNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_NAME_EDIT)), instance, nullptr);

        g_selectedNameButton = CreateWindowW(L"BUTTON", L"Set Name", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_NAME_BUTTON)), instance, nullptr);

        g_selectedInventoryList = CreateInventoryListView(parent, IDC_SELECTED_INVENTORY_LIST, instance);

        g_selectedAmountEdit = CreateWindowW(L"EDIT", L"0.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_AMOUNT_EDIT)), instance, nullptr);
        SetWindowSubclass(g_selectedAmountEdit, NumericEditSubclassProc, 1, 0);

        g_selectedAddButton = CreateWindowW(L"BUTTON", L"Add Resource", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_ADD_BUTTON)), instance, nullptr);

        g_selectedRemoveButton = CreateWindowW(L"BUTTON", L"Delete Resource", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_REMOVE_BUTTON)), instance, nullptr);

        g_selectedSetButton = CreateWindowW(L"BUTTON", L"Center Building", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SELECTED_SET_BUTTON)), instance, nullptr);

        // --- Right column: building currently selected in-game ---
        g_currentNameEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_NAME_EDIT)), instance, nullptr);

        g_currentNameButton = CreateWindowW(L"BUTTON", L"Set Name", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_NAME_BUTTON)), instance, nullptr);

        g_currentInventoryList = CreateInventoryListView(parent, IDC_CURRENT_INVENTORY_LIST, instance);

        g_currentAmountEdit = CreateWindowW(L"EDIT", L"0.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_AMOUNT_EDIT)), instance, nullptr);
        SetWindowSubclass(g_currentAmountEdit, NumericEditSubclassProc, 2, 0);

        g_currentSetAmountButton = CreateWindowW(L"BUTTON", L"Set Amount", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_SET_AMOUNT_BUTTON)), instance, nullptr);

        g_currentAddButton = CreateWindowW(L"BUTTON", L"Add Resource", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_ADD_BUTTON)), instance, nullptr);

        g_currentRemoveButton = CreateWindowW(L"BUTTON", L"Delete Resource", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_REMOVE_BUTTON)), instance, nullptr);

        g_currentSetButton = CreateWindowW(L"BUTTON", L"Center Building", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CURRENT_SET_BUTTON)), instance, nullptr);

        SetSelectedNameEnabled(false);
        SetCurrentNameEnabled(false);

        SetSelectedCenterEnabled(false);
        SetCurrentCenterEnabled(false);

        SetSelectedAddEnabled(false);
        SetCurrentAddEnabled(false);

        SetSelectedAmountControlsEnabled(false);
        SetCurrentAmountControlsEnabled(false);

        SetMoneyControlsEnabled(false);

        EnumChildWindows(parent, [](HWND child, LPARAM) -> BOOL {
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
            return TRUE;
            }, 0);
    }

    void Layout(HWND parent, int clientWidth, int clientHeight)
    {
        const int margin = 10;
        int rowGap = 6;
        const int editHeight = 24;
        const int filterDropdownHeight = 200;

        const int comboYOffset = -2;

        const int columnWidth = (clientWidth - margin * 3) / 2;
        const int leftX = margin;
        const int rightX = leftX + columnWidth + margin;

        int y = margin;
        int currentX = leftX;

        const int connectBtnWidth = 85;
        const int alwaysOnTopWidth = 110;

        int leftColumnRightEdge = leftX + columnWidth;

        int alwaysOnTopX = leftColumnRightEdge - alwaysOnTopWidth;
        MoveWindow(g_alwaysOnTop, alwaysOnTopX, y, alwaysOnTopWidth, editHeight, TRUE);

        int connectBtnX = alwaysOnTopX - rowGap - connectBtnWidth;
        MoveWindow(g_connectButton, connectBtnX, y, connectBtnWidth, editHeight, TRUE);

        int dynamicStatusWidth = connectBtnX - rowGap - leftX;
        MoveWindow(g_status, leftX, y + 2, dynamicStatusWidth, editHeight, TRUE);

        currentX = rightX;
        const int moneyLabelWidth = 130;
        const int newMoneyEditWidth = 90;
        const int setMoneyBtnWidth = 90;
        const int moneyLockWidth = 160;

        MoveWindow(g_moneyLabel, currentX, y + 2, moneyLabelWidth, editHeight, TRUE);
        currentX += moneyLabelWidth + rowGap;
        MoveWindow(g_newMoneyEdit, currentX, y, newMoneyEditWidth, editHeight, TRUE);
        currentX += newMoneyEditWidth + rowGap;
        MoveWindow(g_setMoneyButton, currentX, y, setMoneyBtnWidth, editHeight, TRUE);
        currentX += setMoneyBtnWidth + rowGap;
        MoveWindow(g_moneyLockCheckbox, currentX, y, moneyLockWidth, editHeight, TRUE);


        y += editHeight + margin;
        int columnTop = y;
        int columnHeight = clientHeight - columnTop - margin;

        // ---Left Column---
        currentX = leftX;

        const int typeFilterLabelWidth = 64;
        const int ownedOnlyWidth = 230;

        const int typeFilterComboWidth = columnWidth - typeFilterLabelWidth - ownedOnlyWidth - (rowGap * 2);

        MoveWindow(g_typeFilterLabel, currentX, y + 2, typeFilterLabelWidth, editHeight, TRUE);
        currentX += typeFilterLabelWidth + rowGap;
        MoveWindow(g_typeFilter, currentX, y + comboYOffset, typeFilterComboWidth, editHeight + filterDropdownHeight, TRUE);
        currentX += typeFilterComboWidth + rowGap;
        MoveWindow(g_ownedOnlyCheckbox, currentX, y, ownedOnlyWidth, editHeight, TRUE);

        y += editHeight + rowGap;

        int leftRemaining = columnHeight - (y - columnTop) - (editHeight * 2) - (rowGap * 2);
        int buildingListHeight = leftRemaining / 2;
        int selectedInventoryHeight = leftRemaining - buildingListHeight;

        MoveWindow(g_buildingList, leftX, y, columnWidth, buildingListHeight, TRUE);
        y += buildingListHeight + rowGap;

        const int setNameBtnWidth = 80;
        const int nameEditWidth = columnWidth - setNameBtnWidth - rowGap;
        MoveWindow(g_selectedNameEdit, leftX, y, nameEditWidth, editHeight, TRUE);
        MoveWindow(g_selectedNameButton, leftX + nameEditWidth + rowGap, y, setNameBtnWidth, editHeight, TRUE);
        y += editHeight + rowGap;

        MoveWindow(g_selectedInventoryList, leftX, y, columnWidth, selectedInventoryHeight, TRUE);
        y += selectedInventoryHeight + rowGap;

        const int amountEditWidth = 70;
        const int setAmountBtnWidth = 86;
        const int centerBtnWidth = 108;

        const int addBtnWidth = 98;
        const int removeBtnWidth = addBtnWidth + 16;

        rowGap = 4;

        currentX = leftX;
        MoveWindow(g_selectedAmountEdit, currentX, y, amountEditWidth, editHeight, TRUE);
        currentX += amountEditWidth + rowGap;
        MoveWindow(g_selectedSetAmountButton, currentX, y, setAmountBtnWidth, editHeight, TRUE);
        currentX += setAmountBtnWidth + rowGap;
        MoveWindow(g_selectedAddButton, currentX, y, addBtnWidth, editHeight, TRUE);
        currentX += addBtnWidth + rowGap;
        MoveWindow(g_selectedRemoveButton, currentX, y, removeBtnWidth, editHeight, TRUE);
        currentX += removeBtnWidth + rowGap;
        MoveWindow(g_selectedSetButton, leftX + columnWidth - centerBtnWidth, y, centerBtnWidth, editHeight, TRUE);

        // --- Right Column ---
        y = columnTop;
        currentX = rightX;

        const int currentBuildingLabelWidth = 100;
        const int currentNameEditWidth = columnWidth - currentBuildingLabelWidth - setNameBtnWidth - (rowGap * 2);

        MoveWindow(g_currentBuilding, currentX, y + 2, currentBuildingLabelWidth, editHeight, TRUE);
        currentX += currentBuildingLabelWidth + rowGap;
        MoveWindow(g_currentNameEdit, currentX, y, currentNameEditWidth, editHeight, TRUE);
        currentX += currentNameEditWidth + rowGap;
        MoveWindow(g_currentNameButton, currentX, y, setNameBtnWidth, editHeight, TRUE);
        y += editHeight + rowGap;

        int currentInventoryHeight = columnHeight - (y - columnTop) - editHeight;
        MoveWindow(g_currentInventoryList, rightX, y, columnWidth, currentInventoryHeight, TRUE);
        y += currentInventoryHeight + rowGap;

        currentX = rightX;
        MoveWindow(g_currentAmountEdit, currentX, y, amountEditWidth, editHeight, TRUE);
        currentX += amountEditWidth + rowGap;
        MoveWindow(g_currentSetAmountButton, currentX, y, setAmountBtnWidth, editHeight, TRUE);
        currentX += setAmountBtnWidth + rowGap;
        MoveWindow(g_currentAddButton, currentX, y, addBtnWidth, editHeight, TRUE);
        currentX += addBtnWidth + rowGap;
        MoveWindow(g_currentRemoveButton, currentX, y, removeBtnWidth, editHeight, TRUE);
        currentX += removeBtnWidth + rowGap;
        MoveWindow(g_currentSetButton, rightX + columnWidth - centerBtnWidth, y, centerBtnWidth, editHeight, TRUE);
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

    void SetAlwaysOnTopChecked(bool checked)
    {
        SendMessageW(g_alwaysOnTop, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetAlwaysOnTopChecked()
    {
        return SendMessageW(g_alwaysOnTop, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetConnectButtonState(bool connected)
    {
        SetWindowTextW(g_connectButton, connected ? L"Disconnect" : L"Connect");
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

    void ClearTypeFilter()
    {
        SendMessageW(g_typeFilter, CB_RESETCONTENT, 0, 0);
    }

    void SetTypeFilterEnabled(bool enabled)
    {
        EnableWindow(g_typeFilter, enabled);
    }

    void SetOwnedOnlyChecked(bool checked)
    {
        SendMessageW(g_ownedOnlyCheckbox, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    bool GetOwnedOnlyChecked()
    {
        return SendMessageW(g_ownedOnlyCheckbox, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    void SetOwnedOnlyEnabled(bool enabled)
    {
        EnableWindow(g_ownedOnlyCheckbox, enabled);
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
    }

    void SetSelectedAddEnabled(bool enabled)
    {
        EnableWindow(g_selectedAddButton, enabled);
    }

    void SetSelectedAmountControlsEnabled(bool enabled)
    {
        EnableWindow(g_selectedAmountEdit, enabled);
        EnableWindow(g_selectedSetAmountButton, enabled);
        EnableWindow(g_selectedRemoveButton, enabled);
        if (!enabled)
            SetWindowTextW(g_selectedAmountEdit, L"0.00");
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
    }

    void SetCurrentAddEnabled(bool enabled)
    {
        EnableWindow(g_currentAddButton, enabled);
    }

    void SetCurrentAmountControlsEnabled(bool enabled)
    {
        EnableWindow(g_currentAmountEdit, enabled);
        EnableWindow(g_currentSetAmountButton, enabled);
        EnableWindow(g_currentRemoveButton, enabled);
        if (!enabled)
            SetWindowTextW(g_currentAmountEdit, L"0.00");
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
        // also selects it below) — fill the amount box with that row's
        // current value AND enable it + "Set Amount", since both are now
        // governed by resource selection, not by which building is picked.
        bool justSelected = !(nmlv->uOldState & LVIS_SELECTED) && (nmlv->uNewState & LVIS_SELECTED);
        if (justSelected)
        {
            wchar_t amountText[64] = {};
            ListView_GetItemText(list, nmlv->iItem, 2, amountText, 64);
            SetWindowTextW(amountEdit, amountText);

            if (panel == InventoryPanel::Selected)
                SetSelectedAmountControlsEnabled(true);
            else
                SetCurrentAmountControlsEnabled(true);
        }

        // A row was just DESELECTED (e.g. the list got repopulated without
        // a matching previous selection) — disable again rather than
        // leaving stale controls active for a resource that's no longer
        // picked.
        bool justDeselected = (nmlv->uOldState & LVIS_SELECTED) && !(nmlv->uNewState & LVIS_SELECTED);
        if (justDeselected)
        {
            if (panel == InventoryPanel::Selected)
                SetSelectedAmountControlsEnabled(false);
            else
                SetCurrentAmountControlsEnabled(false);
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

            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN:
            {
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetBkMode(hdc, TRANSPARENT);
                return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
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

        state.amountEdit = CreateWindowW(L"EDIT", L"0.00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            100, 53, 190, 22, dlg, nullptr, nullptr, nullptr);
        SetWindowSubclass(state.amountEdit, NumericEditSubclassProc, 4, 0);

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

    void ResetOnDisconnect()
    {
        SetMoneyDisplay(L"");
        SetNewMoneyEditText(L"0.00");
        SetMoneyLockChecked(false);
        SetMoneyControlsEnabled(false);

        ClearTypeFilter();
        SetTypeFilterEnabled(false);
        SetOwnedOnlyEnabled(false); // checked state deliberately untouched — it's a saved preference, not connection state

        PopulateBuildingList({});

        SetSelectedNameEditText(L"");
        SetSelectedNameEnabled(false);
        PopulateSelectedInventory({}, {});
        SetSelectedAmountControlsEnabled(false);
        SetSelectedCenterEnabled(false);

        SetCurrentNameEditText(L"");
        SetCurrentNameEnabled(false);
        PopulateCurrentInventory({}, {});
        SetCurrentAmountControlsEnabled(false);
        SetCurrentCenterEnabled(false);
    }
}