// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "BrowserWindow.h"
#include "Tab.h"
#include <tlhelp32.h>
#include <Commctrl.h>
#include "asyncutility.h"

using namespace Microsoft::WRL;

Tab::Tab() : DevToolsState(DockState::DS_UNKNOWN) {}

LRESULT CALLBACK Tab::dtWndProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
    if (Tab* tab = reinterpret_cast<Tab*>(dwRefData))
        switch (DockState ds = tab->GetDevToolsState(); msg)
        {
        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                BeginPaint(hWnd, &ps);
                RECT rc;
                GetWindowRect(hWnd, &rc);
                OffsetRect(&rc, -rc.left, -rc.top);
                HDC hdc = GetWindowDC(hWnd);
                auto hpen = CreatePen(PS_SOLID, tab->rzBorderSize, RGB(204, 204, 204));
                auto oldpen = SelectObject(hdc, hpen);
                SelectObject(hdc, GetStockObject(NULL_BRUSH));
                // Draw a straight line that splits the webview and the dev tool window from each other
                MoveToEx(hdc, ds == DockState::DS_DOCK_LEFT ? rc.right : rc.left, rc.top, NULL);
                LineTo(hdc, ds == DockState::DS_DOCK_RIGHT ? rc.left : rc.right, ds == DockState::DS_DOCK_BOTTOM ? rc.top : rc.bottom);
                SelectObject(hdc, oldpen);
                DeleteObject(oldpen);
                ReleaseDC(hWnd, hdc);
                EndPaint(hWnd, &ps);
            }
            break;
        case WM_NCCALCSIZE:
            {
                NCCALCSIZE_PARAMS* sz = (NCCALCSIZE_PARAMS*)lParam;
                auto [rect, state] = tab->tplRect;
                if (!state)
                {
                    GetClientRect(hWnd, &rect);
                    MapWindowPoints(hWnd, GetParent(hWnd), (LPPOINT)&rect, 2);
                    state = true;
                    tab->tplRect = {rect, state};
                }

                if (state)
                {
                    // Get rid of non-client area
                    sz->rgrc[0].left -= rect.left;
                    sz->rgrc[0].right += rect.right;
                    sz->rgrc[0].top -= rect.top;
                    sz->rgrc[0].bottom += rect.bottom;

                    if (ds == DockState::DS_DOCK_RIGHT)
                    {
                        sz->rgrc[0].left += tab->rzBorderSize/2;
                        sz->rgrc[0].right += tab->rzBorderSize/2;
                    }
                    else if (ds == DockState::DS_DOCK_LEFT)
                        sz->rgrc[0].right -= tab->rzBorderSize/2;
                    else if (ds == DockState::DS_DOCK_BOTTOM)
                    {
                        sz->rgrc[0].bottom += tab->rzBorderSize/2;
                        sz->rgrc[0].top += tab->rzBorderSize/2;
                    }
                }
            }
            break;
        case WM_NCHITTEST:
            {
                LRESULT lrDef = DefSubclassProc(hWnd, msg, wParam, lParam);
                int rightDirection = -10; // Default value, nothing special. You get HTERROR(-2) minimum.
                switch (ds)
                {
                    case DockState::DS_DOCK_RIGHT: // If docked right,
                        rightDirection = HTLEFT; // then we can only resize to left side
                        break;
                    case DockState::DS_DOCK_LEFT:
                        rightDirection = HTRIGHT;
                        break;
                    case DockState::DS_DOCK_BOTTOM:
                        rightDirection = HTTOP;
                        break;
                }

                if (rightDirection != -10)
                    for (int i = HTLEFT; i < HTBOTTOMRIGHT+1; ++i)
                        if (lrDef == i && i != rightDirection)
                            return HTCLIENT;

                return lrDef;
            }
            break;
        case WM_SIZING:
            {
                PRECT rectp = (PRECT)lParam;
                tab->DockDataMap.at(ds)->nWidth = rectp->right - rectp->left;
                tab->DockDataMap.at(ds)->nHeight = rectp->bottom - rectp->top;
                switch (ds)
                {
                    case DockState::DS_DOCK_LEFT:
                    case DockState::DS_DOCK_RIGHT:
                        if (wParam == WMSZ_LEFT || wParam == WMSZ_RIGHT)
                            tab->DockDataMap.at(ds)->nWidth -= tab->rzBorderSize;
                        break;
                    case DockState::DS_DOCK_BOTTOM:
                        if (wParam == WMSZ_TOP)
                            tab->DockDataMap.at(ds)->nHeight -= tab->rzBorderSize;
                        break;
                }

                tab->ResizeWebView(true);
            }
            break;
        case WM_ENTERSIZEMOVE:
            if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0) // Avoid further WM_ENTERSIZEMOVE
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            break;
        case WM_DESTROY:
            RemoveWindowSubclass(hWnd, dtWndProcStatic, uIdSubclass);
            break;
        }
    return DefSubclassProc(hWnd, msg, wParam, lParam);
}

BOOL CALLBACK Tab::EnumWindowsProcStatic(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    if (Tab* tab = reinterpret_cast<Tab*>(lParam); tab != nullptr)
        return tab->EnumWindowsProc(hwnd, reinterpret_cast<LPARAM>(tab->m_parentHWnd));
    return FALSE;
}

BOOL CALLBACK Tab::EnumWindowsProc(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    WCHAR classname[256];
	GetClassName(hwnd, classname, sizeof(classname));
    if (wcscmp(classname, L"Chrome_WidgetWin_1") != 0 || GetParent(hwnd) != NULL && !(GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD))
        return TRUE;

    if (possible_PID == pid)
    {
        if (BrowserWindow* bw = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(reinterpret_cast<HWND>(lParam), GWLP_USERDATA)); bw->CheckDTOwnership(hwnd))
            return TRUE; // Skip, owned by other tab

        m_devtHWnd = hwnd;
        pid_DevTools = GetWindowThreadProcessId(hwnd, NULL);
        DevToolsState = DockState::DS_UNDOCK;
        return FALSE;
    }
    return TRUE;
}

std::unique_ptr<Tab> Tab::CreateNewTab(HWND hWnd, ICoreWebView2Environment* env, size_t id, bool shouldBeActive)
{
    std::unique_ptr<Tab> tab = std::make_unique<Tab>();

    tab->m_parentHWnd = hWnd;
    tab->m_tabId = id;
    tab->SetMessageBroker();
    tab->Init(env, shouldBeActive);

    return tab;
}

HRESULT Tab::Init(ICoreWebView2Environment* env, bool shouldBeActive)
{
    return env->CreateCoreWebView2Controller(m_parentHWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
        [this, shouldBeActive](HRESULT result, ICoreWebView2Controller* host) -> HRESULT {
        if (!SUCCEEDED(result))
        {
            OutputDebugString(L"Tab WebView creation failed\n");
            return result;
        }
        m_contentController = host;
        BrowserWindow::CheckFailure(m_contentController->get_CoreWebView2(&m_contentWebView), L"");
        BrowserWindow* browserWindow = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA));
        RETURN_IF_FAILED(m_contentWebView->add_WebMessageReceived(m_messageBroker.Get(), &m_messageBrokerToken));

        // Register event handler for history change
        RETURN_IF_FAILED(m_contentWebView->add_HistoryChanged(Callback<ICoreWebView2HistoryChangedEventHandler>(
            [this, browserWindow](ICoreWebView2* webview, IUnknown* args) -> HRESULT
        {
            BrowserWindow::CheckFailure(browserWindow->HandleTabHistoryUpdate(m_tabId, webview), L"Can't update go back/forward buttons.");

            return S_OK;
        }).Get(), &m_historyUpdateForwarderToken));

        // Register event handler for source change
        RETURN_IF_FAILED(m_contentWebView->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
            [this, browserWindow](ICoreWebView2* webview, ICoreWebView2SourceChangedEventArgs* args) -> HRESULT
        {
            BrowserWindow::CheckFailure(browserWindow->HandleTabURIUpdate(m_tabId, webview), L"Can't update address bar");

            return S_OK;
        }).Get(), &m_uriUpdateForwarderToken));

        RETURN_IF_FAILED(m_contentWebView->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
            [this, browserWindow](ICoreWebView2* webview, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT
        {
            BrowserWindow::CheckFailure(browserWindow->HandleTabNavStarting(m_tabId, webview), L"Can't update reload button");

            return S_OK;
        }).Get(), &m_navStartingToken));

        RETURN_IF_FAILED(m_contentWebView->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [this, browserWindow](ICoreWebView2* webview, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
        {
            BrowserWindow::CheckFailure(browserWindow->HandleTabNavCompleted(m_tabId, webview, args), L"Can't update reload button");
            return S_OK;
        }).Get(), &m_navCompletedToken));

        // Enable listening for security events to update secure icon
        RETURN_IF_FAILED(m_contentWebView->CallDevToolsProtocolMethod(L"Security.enable", L"{}", nullptr));

        BrowserWindow::CheckFailure(m_contentWebView->GetDevToolsProtocolEventReceiver(L"Security.securityStateChanged", &m_securityStateChangedReceiver), L"");

        // Forward security status updates to browser
        RETURN_IF_FAILED(m_securityStateChangedReceiver->add_DevToolsProtocolEventReceived(Callback<ICoreWebView2DevToolsProtocolEventReceivedEventHandler>(
            [this, browserWindow](ICoreWebView2* webview, ICoreWebView2DevToolsProtocolEventReceivedEventArgs* args) -> HRESULT
        {
            BrowserWindow::CheckFailure(browserWindow->HandleTabSecurityUpdate(m_tabId, webview, args), L"Can't update security icon");
            return S_OK;
        }).Get(), &m_securityUpdateToken));

        RETURN_IF_FAILED(m_contentWebView->Navigate(L"https://www.bing.com"));
        // Register a handler for the AcceleratorKeyPressed event.
        RETURN_IF_FAILED(m_contentController->add_AcceleratorKeyPressed(Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
            [this](ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT
        {
                COREWEBVIEW2_KEY_EVENT_KIND kind;
                RETURN_IF_FAILED(args->get_KeyEventKind(&kind));
                if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN)
                {
                    UINT key;
                    RETURN_IF_FAILED(args->get_VirtualKey(&key));
                    if (key == 0x44) // "D"
                    {
                        m_HWnd = GetFocus();
                        COREWEBVIEW2_PHYSICAL_KEY_STATUS status;
                        RETURN_IF_FAILED(args->get_PhysicalKeyStatus(&status));
                        if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) // CTRL + SHIFT + D
                        {
                            RETURN_IF_FAILED(args->put_Handled(TRUE));

                            auto before = [this]
                            {
                                DockState state = GetDevToolsState();
                                if (HWND hwnd = GetDevTools(); hwnd == nullptr && state == DockState::DS_UNKNOWN)
                                    FindDevTools();
                                return true;
                            };

                            auto after = [this]
                            {
                                DockState state = GetDevToolsState();
                                if (HWND hwnd = GetDevTools(); hwnd != nullptr && state == DockState::DS_UNKNOWN)
                                    state = DockState::DS_UNDOCK;

                                if (state != DockState::DS_UNKNOWN)
                                    DockDevTools(state != DockState::DS_AMOUNT+(-1) ? state+1 : DockState::DS_UNDOCK); // Determine the next dock position
                            };

                            // Perform the action asynchronously to avoid blocking the browser process's event queue.
                            async_future<bool>(before, after);
                        }
                    }
                }

            return S_OK;
        }).Get(), &m_acceleratorKeyPressedToken));

        browserWindow->HandleTabCreated(m_tabId, shouldBeActive);

        return S_OK;
    }).Get());
}

void Tab::SetMessageBroker()
{
    m_messageBroker = Callback<ICoreWebView2WebMessageReceivedEventHandler>(
        [this](ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* eventArgs) -> HRESULT
    {
        BrowserWindow* browserWindow = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA));
        BrowserWindow::CheckFailure(browserWindow->HandleTabMessageReceived(m_tabId, webview, eventArgs), L"");

        return S_OK;
    });
}

HRESULT Tab::ResizeWebView(bool recalculate)
{
    RECT bounds;
    GetClientRect(m_parentHWnd, &bounds);

    BrowserWindow* browserWindow = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA));
    bounds.top += browserWindow->GetDPIAwareBound(BrowserWindow::c_uiBarHeight);
    
    CalculateDockData(bounds); // Calculate initial dock data 

    if (DockState ds = GetDevToolsState(); ds != DockState::DS_UNKNOWN)
    {
        RECT windowRect;
        GetWindowRect(m_parentHWnd, &windowRect);
        if (int bordersWidth = (windowRect.right - windowRect.left) - bounds.right; ds == DockState::DS_DOCK_RIGHT)
        {
            bounds.right = windowRect.right - windowRect.left - DockDataMap.at(ds)->nWidth - bordersWidth - rzBorderSize;
            DockDataMap.at(ds)->X = bounds.right;
            DockDataMap.at(ds)->nHeight = bounds.bottom - bounds.top;
        }
        else if (LONG old = bounds.left; ds == DockState::DS_DOCK_LEFT)
        {
            bounds.left += DockDataMap.at(ds)->nWidth + rzBorderSize;
            DockDataMap.at(ds)->X = old;
            DockDataMap.at(ds)->nHeight = bounds.bottom - bounds.top;
        }
        else if (ds == DockState::DS_DOCK_BOTTOM)
        {
            bounds.bottom -= DockDataMap.at(ds)->nHeight + rzBorderSize;
            DockDataMap.at(ds)->Y = bounds.bottom;
            DockDataMap.at(ds)->nWidth = bounds.right - bounds.left;
        }

        if (!recalculate)
            if (auto itr = DockDataMap.find(ds); itr != DockDataMap.end())
            {
                MoveWindow(m_devtHolderHWnd == nullptr ? m_devtHWnd : m_devtHolderHWnd, itr->second->X, itr->second->Y,
                    itr->second->nWidth + (ds != DockState::DS_DOCK_BOTTOM ? rzBorderSize : 0),
                    itr->second->nHeight + (ds == DockState::DS_DOCK_BOTTOM ? rzBorderSize : 0), true);
                ShowWindow(m_devtHolderHWnd == nullptr ? m_devtHWnd : m_devtHolderHWnd, SW_SHOW);
            }

        if (ds != DockState::DS_UNDOCK)
        {
            // m_devtHWnd has borders so we have to take that into account
            RECT rect[2];
            GetClientRect(m_devtHWnd, &rect[0]);
            GetWindowRect(m_devtHWnd, &rect[1]);
            int dt_bordersWidth = rect[1].right - rect[1].left - rect[0].right;
            int dt_bordersHeight = rect[1].bottom - rect[1].top - rect[0].bottom;

            int titleBarHeight = GetSystemMetricsForDpi(SM_CYCAPTION, GetDpiForWindow(m_devtHWnd)) + GetSystemMetricsForDpi(SM_CYSIZEFRAME, GetDpiForWindow(m_devtHWnd))
                + GetSystemMetricsForDpi(SM_CYEDGE, GetDpiForWindow(m_devtHWnd)) * 2;
            // Below we hide the title bar and the borders of m_devtHWnd. Then we extend the height and the width to fill the gaps...
            MoveWindow(m_devtHWnd, -dt_bordersWidth/2, -titleBarHeight,
                DockDataMap.at(ds)->nWidth + dt_bordersWidth + (ds != DockState::DS_DOCK_BOTTOM ? rzBorderSize/2 : 0),
                DockDataMap.at(ds)->nHeight + dt_bordersHeight + titleBarHeight + rzBorderSize/2, true);
        }
    }

    return m_contentController->put_Bounds(bounds);
}

void Tab::FindDevTools() 
{
    PROCESSENTRY32 pe32;
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;

    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(h, &pe32))
    {
        do
        {
            if (GetCurrentProcessId() == pe32.th32ParentProcessID) // PID of the parents must match
            {
                possible_PID = pe32.th32ProcessID;
                if (EnumWindows(EnumWindowsProcStatic, reinterpret_cast<LPARAM>(this)) == FALSE)
                    break;
            }
        } while (Process32Next(h, &pe32));
    }
    CloseHandle(h);
}

HWND Tab::GetDevTools()
{
    // Both HWND and PID are recycled. So make sure that the devtools are really exist!
    if (m_devtHWnd != nullptr && IsWindow(m_devtHWnd) && GetWindowThreadProcessId(m_devtHWnd, NULL) == pid_DevTools)
        return m_devtHWnd;
    return nullptr;
}

void Tab::DockDevTools(DockState state)
{
    if (GetDevToolsState() == DockState::DS_UNKNOWN)
        return;

    DevToolsState = state;

    if (state == DockState::DS_UNDOCK)
    {
        SetParent(m_devtHWnd, nullptr);
        SetWindowLong(m_devtHWnd, GWL_STYLE, GetWindowLong(m_devtHWnd, GWL_STYLE) & ~WS_CHILD);
        DestroyWindow(m_devtHolderHWnd);
        m_devtHolderHWnd = nullptr;

        DevToolsState = DockState::DS_UNDOCK;
        // Move the window back to it's original position
        if (auto itr = DockDataMap.find(DevToolsState); itr != DockDataMap.end())
            MoveWindow(m_devtHWnd, itr->second->X, itr->second->Y, itr->second->nWidth + rzBorderSize, itr->second->nHeight, true);
        DockDataMap.erase(DockState::DS_UNDOCK);
    }
    else // DOCK
    {
        if (state == DockState::DS_DOCK_RIGHT && DockDataMap.find(DockState::DS_UNDOCK) == DockDataMap.end())
        {
            RECT undockedRect;
            GetWindowRect(m_devtHWnd, &undockedRect);
            DockDataMap.insert(std::make_pair(DockState::DS_UNDOCK, std::make_unique<DockData>(undockedRect.left, undockedRect.top, undockedRect.right - undockedRect.left, undockedRect.bottom - undockedRect.top)));
        }

        WNDCLASS wc = {};
        wc.lpfnWndProc   = DefWindowProc;
        wc.hInstance     = GetModuleHandle(NULL);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpszClassName = L"Developer_Tools_Holder";
        ::RegisterClass(&wc);
        
        if (m_devtHolderHWnd == nullptr && !IsWindow(m_devtHolderHWnd))
        {
            m_devtHolderHWnd = CreateWindowEx(0,wc.lpszClassName, L"", WS_CHILD | WS_SIZEBOX, 0,0,0,0, m_parentHWnd, NULL, GetModuleHandle(NULL), NULL);
            SetWindowSubclass(m_devtHolderHWnd, dtWndProcStatic, 1, (DWORD_PTR) this);
            SetParent(m_devtHWnd, m_devtHolderHWnd);
            SetWindowLong(m_devtHWnd, GWL_STYLE, GetWindowLong(m_devtHWnd, GWL_STYLE) | WS_CHILD);
        }
        
        if (m_devtHolderHWnd != nullptr)
            SetWindowPos(m_devtHolderHWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
        SetWindowPos(m_devtHWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
    }

    if (state != DockState::DS_UNDOCK)
        SetFocus(m_HWnd); // Docking/Undocking the DevTools cause loss of focus. And the AcceleratorKeyPressed event is only set for this window.

    ResizeWebView();
}

DockState Tab::GetDevToolsState()
{
    if (HWND hwnd = GetDevTools(); hwnd == nullptr)
        DevToolsState = DockState::DS_UNKNOWN;

    return DevToolsState;
}

void Tab::CalculateDockData(RECT bounds)
{
    if (!DockDataMap.empty())
        return;

    DockDataMap.insert(std::make_pair(DockState::DS_DOCK_RIGHT, std::make_unique<DockData>(0,0,0,0)));
    DockDataMap.insert(std::make_pair(DockState::DS_DOCK_LEFT, std::make_unique<DockData>(0,0,0,0)));
    DockDataMap.insert(std::make_pair(DockState::DS_DOCK_BOTTOM, std::make_unique<DockData>(0,0,0,0)));
    
    for (auto i = DockDataMap.begin(); i != DockDataMap.end(); ++i)
    {
        i->second->X = int(round(DockRatioMap.at(i->first).XWidth * (i->first == DockState::DS_DOCK_RIGHT ? bounds.right : bounds.left)));

        switch (BrowserWindow* browserWindow = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA)); i->first)
        {
            case DockState::DS_DOCK_RIGHT:
            case DockState::DS_DOCK_LEFT:
                i->second->Y = int(round(DockRatioMap.at(i->first).YHeight * bounds.top));
                i->second->nWidth = int(round(bounds.right - DockRatioMap.at(i->first).XWidth * bounds.right));
                i->second->nHeight = int(round(DockRatioMap.at(i->first).YHeight * (bounds.bottom - bounds.top)));
            break;
            case DockState::DS_DOCK_BOTTOM:
                i->second->Y = browserWindow->GetDPIAwareBound(BrowserWindow::c_uiBarHeight) + int(round(DockRatioMap.at(i->first).YHeight * (bounds.bottom - bounds.top)));
                i->second->nWidth = int(round(DockRatioMap.at(i->first).XWidth * bounds.right - bounds.left));
                i->second->nHeight = int(round((1.0f - DockRatioMap.at(i->first).YHeight) * (bounds.bottom - bounds.top)));
            break;
        }
    }
}