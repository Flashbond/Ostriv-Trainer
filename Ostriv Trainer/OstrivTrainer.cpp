#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' " \
    "name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' " \
    "publicKeyToken='6595b64144ccf1df' language='*'\"")


#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>
#include <CommCtrl.h>
#include <memory>

#include "UI/UI.h"
#include "Core/ProcessManager.h"
#include "Core/RemoteMemory.h"
#include "Game/Building.h"
#include "Game/ResourceManager.h"
#include "Game/BuildingManager.h"
#include "Game/SelectionTracker.h"
#include "Game/OstrivOffsets.h"
#include "Game/MoneyController.h"
#include "Json/JsonManager.h"
#include "Interface/InterfaceManager.h"
#include "Camera/CameraController.h"

#pragma comment(lib, "comctl32.lib")

namespace
{
    constexpr UINT_PTR kFastTimerId = 1;
    constexpr UINT kFastIntervalMs = 200;

    constexpr UINT_PTR kSlowTimerId = 2;
    constexpr UINT kSlowIntervalMs = 1000; // BuildingManager::Tick() advances one phase per call; a full sync cycle takes 5 calls (5s)

    ProcessManager g_processManager;
    RemoteMemory g_remoteMemory;
    JsonManager g_jsonManager;

    std::unique_ptr<ResourceManager> g_resourceManager;
    std::unique_ptr<BuildingManager> g_buildingManager;
    std::unique_ptr<SelectionTracker> g_selectionTracker;
    std::unique_ptr<CameraController> g_cameraController;
    std::unique_ptr<MoneyController> g_moneyController;
    std::unique_ptr<InterfaceManager> g_interfaceManager;

    bool g_connected = false;

    bool Connect(HWND hwnd)
    {
        if (!g_processManager.FindProcess(Ostriv::PROCESS_NAME))
        {
            UI::SetStatus(L"Process not found: ostriv.exe is not running.");
            return false;
        }

        if (!g_remoteMemory.Attach(g_processManager.GetProcessId()))
        {
            UI::SetStatus(L"Failed to attach to the process.");
            return false;
        }

        uintptr_t moduleBase = g_remoteMemory.GetModuleBase();

        g_resourceManager = std::make_unique<ResourceManager>(g_remoteMemory, moduleBase);
        g_resourceManager->BuildResourceCache(); // one-time scan, feeds the "+" dialog's dropdown

        g_buildingManager = std::make_unique<BuildingManager>(g_remoteMemory, *g_resourceManager, moduleBase);
        g_selectionTracker = std::make_unique<SelectionTracker>(g_remoteMemory);

        if (!g_buildingManager->ResolveBuildingTable())
        {
            // The process handle and a few backend objects are already
            // live at this point — rather than leaving the Connect button
            // clickable (inviting a retry on top of that half-built
            // state), force the button into "Disconnect" so clicking it
            // is the only path forward. Disconnect() itself tears
            // everything down cleanly, null-object guards make it safe to
            // call even though g_interfaceManager was never created.
            g_connected = true;
            UI::SetConnectButtonState(true);
            UI::SetStatus(L"Attached, but could not resolve building table.");
            return false;
        }

        bool selectionHookInstalled = g_selectionTracker->Install();

        g_cameraController = std::make_unique<CameraController>(g_remoteMemory);
        bool cameraHookInstalled = g_cameraController->Install();

        g_moneyController = std::make_unique<MoneyController>(g_remoteMemory);
        bool moneyResolved = g_moneyController->Resolve();

        g_interfaceManager = std::make_unique<InterfaceManager>(
            *g_buildingManager, *g_resourceManager, g_jsonManager,
            g_selectionTracker.get(), g_cameraController.get(), g_moneyController.get());

        if (moneyResolved)
        {
            UI::SetMoneyControlsEnabled(true);

            double currentMoney = 0.0;
            if (g_moneyController->GetMoney(currentMoney))
            {
                wchar_t buffer[64];
                swprintf_s(buffer, L"%.2f", currentMoney);
                UI::SetNewMoneyEditText(buffer);
            }
        }

        UI::SetOwnedOnlyEnabled(true);
        UI::SetTypeFilterEnabled(true);

        g_connected = true;

        std::wstring status = L"Connected: PID " + std::to_wstring(g_processManager.GetProcessId());
        if (!selectionHookInstalled)
            status += L"  [selection hook failed: " + g_selectionTracker->GetLastError() + L"]";
        if (!cameraHookInstalled)
            status += L"  [camera hook failed: " + g_cameraController->GetLastError() + L"]";
        if (!moneyResolved)
            status += L"  [money signature not found]";

        UI::SetStatus(status);

        for (int i = 0; i < 4; ++i)
            g_interfaceManager->SlowUpdate();

        SetTimer(hwnd, kFastTimerId, kFastIntervalMs, nullptr);
        SetTimer(hwnd, kSlowTimerId, kSlowIntervalMs, nullptr);

        UI::SetConnectButtonState(true);

        return true;
    }

    void Disconnect(HWND hwnd)
    {
        KillTimer(hwnd, kFastTimerId);
        KillTimer(hwnd, kSlowTimerId);

        if (g_selectionTracker)
            g_selectionTracker->Uninstall();

        if (g_cameraController)
            g_cameraController->Uninstall();

        g_interfaceManager.reset();
        g_selectionTracker.reset();
        g_cameraController.reset();
        g_moneyController.reset();
        g_buildingManager.reset();
        g_resourceManager.reset();

        g_remoteMemory.Detach();
        g_connected = false;

        Building::ResetDictionaryCache();

        UI::SetConnectButtonState(false);
        UI::ResetOnDisconnect();
        UI::SetStatus(L"Disconnected");
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            UI::CreateControls(hwnd, reinterpret_cast<LPCREATESTRUCT>(lParam)->hInstance);

            RECT rect;
            GetClientRect(hwnd, &rect);
            UI::Layout(hwnd, rect.right - rect.left, rect.bottom - rect.top);

            // Restore persisted preferences immediately, independent of
            // whether the user has connected yet — these are local
            // settings, not game state, so there's no reason to wait.
            g_jsonManager.Load();

            bool ownedOnly = g_jsonManager.GetShowOnlyOwnedTypes();
            UI::SetOwnedOnlyChecked(ownedOnly);

            bool alwaysOnTop = g_jsonManager.GetAlwaysOnTop();
            UI::SetAlwaysOnTopChecked(alwaysOnTop);
            if (alwaysOnTop)
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

            // Both start disabled at launch — they're only meaningful once
            // connected, even though their checked state is already
            // restored above.
            UI::SetOwnedOnlyEnabled(false);
            UI::SetTypeFilterEnabled(false);

            UI::SetStatus(L"Not connected.");
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            LPMINMAXINFO lpmmi = (LPMINMAXINFO)lParam;

            const int minWidth = 1030;
            const int minHeight = 650;

            lpmmi->ptMinTrackSize.x = minWidth;
            lpmmi->ptMinTrackSize.y = minHeight;

            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            UI::Layout(hwnd, width, height);

            RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }

        case WM_TIMER:
            if (!g_connected || !g_interfaceManager)
                return 0;

            if (wParam == kFastTimerId)
                g_interfaceManager->FastUpdate();
            else if (wParam == kSlowTimerId)
                g_interfaceManager->SlowUpdate();

            return 0;

        case WM_NOTIFY: {
            LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);

            if ((nmhdr->idFrom == UI::IDC_CURRENT_INVENTORY_LIST ||
                nmhdr->idFrom == UI::IDC_SELECTED_INVENTORY_LIST ||
                nmhdr->idFrom == UI::IDC_BUILDING_LIST) && nmhdr->code == NM_CUSTOMDRAW) {

                LPNMLVCUSTOMDRAW lplvcd = reinterpret_cast<LPNMLVCUSTOMDRAW>(lParam);

                switch (lplvcd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;

                case CDDS_ITEMPREPAINT:
                    return CDRF_NOTIFYSUBITEMDRAW;

                case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
                    if (ListView_GetItemState(nmhdr->hwndFrom, lplvcd->nmcd.dwItemSpec, LVIS_SELECTED) == LVIS_SELECTED) {

                        lplvcd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);

                        lplvcd->clrTextBk = GetSysColor(COLOR_HIGHLIGHT);
                        lplvcd->clrText = GetSysColor(COLOR_HIGHLIGHTTEXT);

                        return CDRF_NEWFONT;
                    }

                    return CDRF_DODEFAULT;
                }
                }
            }

            UI::InventoryPanel panel;
            int32_t resourceId = -1;

            if (g_interfaceManager && UI::HandleInventoryCheckboxNotification(lParam, panel, resourceId))
            {
                if (panel == UI::InventoryPanel::Selected)
                    g_interfaceManager->ClearSelectedResourceLock(resourceId);
                else
                    g_interfaceManager->ClearCurrentResourceLock(resourceId);

                return 0;
            }

            auto* header = reinterpret_cast<LPNMHDR>(lParam);

            if (header->idFrom == UI::IDC_BUILDING_LIST && header->code == LVN_ITEMCHANGED && g_interfaceManager)
            {
                auto* nmlv = reinterpret_cast<LPNMLISTVIEW>(lParam);
                if (nmlv->uNewState & LVIS_SELECTED)
                    g_interfaceManager->SelectBuilding(UI::GetSelectedBuildingAddress());
            }

            return 0;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            int notification = HIWORD(wParam);

            if (id == UI::IDC_CONNECT_BUTTON && notification == BN_CLICKED)
            {
                if (!g_connected)
                    Connect(hwnd);
                else
                    Disconnect(hwnd);
                return 0;
            }

            if (id == UI::IDC_ALWAYS_ON_TOP_CHECKBOX && notification == BN_CLICKED)
            {
                bool onTop = UI::GetAlwaysOnTopChecked();
                SetWindowPos(hwnd, onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                    0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

                g_jsonManager.SetAlwaysOnTop(onTop);
                g_jsonManager.Save();
                return 0;
            }

            if (id == UI::IDC_OWNED_ONLY_CHECKBOX && notification == BN_CLICKED)
            {
                bool checked = UI::GetOwnedOnlyChecked();

                if (g_interfaceManager)
                {
                    g_interfaceManager->SetShowOnlyOwnedTypes(checked); // saves + refreshes the live list itself
                }
                else
                {
                    // Not connected yet — nothing live to refresh, just persist.
                    g_jsonManager.SetShowOnlyOwnedTypes(checked);
                    g_jsonManager.Save();
                }

                return 0;
            }

            if (!g_interfaceManager)
                return 0;

            if (id == UI::IDC_TYPE_FILTER && notification == CBN_SELENDOK)
            {
                g_interfaceManager->SetTypeFilter(UI::GetSelectedTypeFilter());
                return 0;
            }
         
            if (id == UI::IDC_SELECTED_SET_BUTTON && notification == BN_CLICKED)
            {
                g_interfaceManager->CenterOnSelectedBuilding();
                return 0;
            }

            if (id == UI::IDC_SELECTED_SET_AMOUNT_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                std::wstring amountText;
                bool locked = false;

                if (UI::GetSelectedInventoryRow(resourceId, amountText, locked) && resourceId != -1)
                {
                    float amount = wcstof(amountText.c_str(), nullptr);
                    g_interfaceManager->SetSelectedResourceAmount(resourceId, amount, locked);
                }
                return 0;
            }

            if (id == UI::IDC_SELECTED_ADD_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                float amount = 0.0f;

                if (UI::ShowAddResourceDialog(hwnd, g_interfaceManager->GetKnownResources(), resourceId, amount))
                    g_interfaceManager->AddSelectedResource(resourceId, amount);

                return 0;
            }

            if (id == UI::IDC_SELECTED_REMOVE_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                std::wstring amountText;
                bool locked = false;

                if (UI::GetSelectedInventoryRow(resourceId, amountText, locked) && resourceId != -1)
                    g_interfaceManager->RemoveSelectedResource(resourceId);

                return 0;
            }

            if (id == UI::IDC_SELECTED_NAME_BUTTON && notification == BN_CLICKED)
            {
                g_interfaceManager->SetSelectedBuildingCustomName(UI::GetSelectedNameEditText());
                return 0;
            }

            if (id == UI::IDC_CURRENT_SET_BUTTON && notification == BN_CLICKED)
            {
                g_interfaceManager->CenterOnCurrentBuilding();
                return 0;
            }

            if (id == UI::IDC_CURRENT_SET_AMOUNT_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                std::wstring amountText;
                bool locked = false;

                if (UI::GetCurrentInventoryRow(resourceId, amountText, locked) && resourceId != -1)
                {
                    float amount = wcstof(amountText.c_str(), nullptr);
                    g_interfaceManager->SetCurrentResourceAmount(resourceId, amount, locked);
                }
                return 0;
            }

            if (id == UI::IDC_CURRENT_ADD_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                float amount = 0.0f;

                if (UI::ShowAddResourceDialog(hwnd, g_interfaceManager->GetKnownResources(), resourceId, amount))
                    g_interfaceManager->AddCurrentResource(resourceId, amount);

                return 0;
            }

            if (id == UI::IDC_CURRENT_REMOVE_BUTTON && notification == BN_CLICKED)
            {
                int32_t resourceId = -1;
                std::wstring amountText;
                bool locked = false;

                if (UI::GetCurrentInventoryRow(resourceId, amountText, locked) && resourceId != -1)
                    g_interfaceManager->RemoveCurrentResource(resourceId);

                return 0;
            }

            if (id == UI::IDC_CURRENT_NAME_BUTTON && notification == BN_CLICKED)
            {
                g_interfaceManager->SetCurrentBuildingCustomName(UI::GetCurrentNameEditText());
                return 0;
            }

            if (id == UI::IDC_SET_MONEY_BUTTON && notification == BN_CLICKED)
            {
                g_interfaceManager->SetMoney(UI::GetNewMoneyEditText(), UI::GetMoneyLockChecked());
                return 0;
            }

            if (id == UI::IDC_MONEY_LOCK_CHECKBOX && notification == BN_CLICKED)
            {
                // Unlike the per-resource checkboxes, this one always
                // notifies immediately (it's a plain button checkbox, not
                // a ListView row) — so unchecking it here IS the genuine
                // user action, no suppression logic needed.
                if (!UI::GetMoneyLockChecked())
                    g_interfaceManager->ClearMoneyLock();
                return 0;
            }

            return 0;
        }
       
        case WM_DESTROY:
            Disconnect(hwnd);
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd)
{
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    const wchar_t CLASS_NAME[] = L"OstrivTrainerWindow";

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"Ostriv Trainer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
        return 0;

    ShowWindow(hwnd, nShowCmd);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}