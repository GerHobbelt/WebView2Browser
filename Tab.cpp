// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "BrowserWindow.h"
#include "Tab.h"
#include <tlhelp32.h>

using namespace Microsoft::WRL;

Tab::Tab() : pid_DevTools(0), DevToolsState(DockState::DS_UNKNOWN) {}

BOOL CALLBACK Tab::EnumWindowsProcStatic(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    if (Tab* tab = reinterpret_cast<Tab*>(lParam); tab != nullptr)
        return tab->EnumWindowsProc(hwnd, 0);
    return FALSE;
}

BOOL CALLBACK Tab::EnumWindowsProc(_In_ HWND hwnd, _In_ LPARAM lParam)
{
    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    WCHAR classname[256];
	GetClassName(hwnd, classname, sizeof(classname));
    if (wcscmp(classname, L"Chrome_WidgetWin_1") != 0 || GetParent(hwnd) != NULL)
        return TRUE;

    if (possible_PID == pid)
    {
        hwnd_DevTools = hwnd;
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
                        COREWEBVIEW2_PHYSICAL_KEY_STATUS status;
                        RETURN_IF_FAILED(args->get_PhysicalKeyStatus(&status));
                        if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000)) // CTRL + SHIFT + D
                        {
                            RETURN_IF_FAILED(args->put_Handled(TRUE));
                            DockState state = GetDevToolsState();
                            if (state == DockState::DS_UNKNOWN)
                                FindDevTools();
                            state = GetDevToolsState();
                            if (state != DockState::DS_UNKNOWN)
                                DockDevTools(state != DockState::DS_AMOUNT+(-1) ? state+1 : DockState::DS_UNDOCK); // Determine the next dock position
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

HRESULT Tab::ResizeWebView()
{
    RECT bounds;
    GetClientRect(m_parentHWnd, &bounds);

    BrowserWindow* browserWindow = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA));
    bounds.top += browserWindow->GetDPIAwareBound(BrowserWindow::c_uiBarHeight);
    
    switch (int half = bounds.right * 50 / 100; GetDevToolsState())
    {
        case DockState::DS_UNDOCK:
            MoveWindow(hwnd_DevTools, prevRect.left, prevRect.top, prevRect.right - prevRect.left, prevRect.bottom - prevRect.top, true);
            break;
        case DockState::DS_DOCKPOS1:
            MoveWindow(hwnd_DevTools, half, bounds.top, bounds.right - half, bounds.bottom - bounds.top, true);
            bounds.right = half;
        break;
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
    if (hwnd_DevTools != nullptr && IsWindow(hwnd_DevTools) && GetWindowThreadProcessId(hwnd_DevTools, NULL) == pid_DevTools)
        return hwnd_DevTools;
    return nullptr;
}

void Tab::DockDevTools(DockState state)
{
    if (GetDevToolsState() == DockState::DS_UNKNOWN)
        return;

    DevToolsState = state;

    LONG lStyle = GetWindowLong(hwnd_DevTools, GWL_STYLE);
    UINT uFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED;
    if (state == DockState::DS_UNDOCK)
    {
        SetParent(hwnd_DevTools, nullptr);
        uFlags |= SWP_NOZORDER;
    }
    else // DOCK
    {
        GetWindowRect(hwnd_DevTools, &prevRect);
        SetParent(hwnd_DevTools, m_parentHWnd);
        lStyle &= ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_OVERLAPPED | WS_THICKFRAME);
    }
    
    SetWindowLong(hwnd_DevTools, GWL_STYLE, lStyle);
    SetWindowPos(hwnd_DevTools, HWND_TOP, 0, 0, 0, 0, uFlags);
    
    if (HWND hwndThis = GetWindowHandle(); hwndThis != nullptr)
        SetFocus(hwndThis); // Docking/Undocking the DevTools cause loss of focus. And the AcceleratorKeyPressed event is only set for this window.

    ResizeWebView();
}

DockState Tab::GetDevToolsState()
{
    if (HWND hwnd = GetDevTools(); hwnd == nullptr)
        DevToolsState = DockState::DS_UNKNOWN;

    return DevToolsState;
}

HWND Tab::GetWindowHandle()
{
    BrowserWindow* browser_window = reinterpret_cast<BrowserWindow*>(GetWindowLongPtr(m_parentHWnd, GWLP_USERDATA));
    HWND childhwnd = GetNextWindow(GetTopWindow(m_parentHWnd), GW_HWNDLAST);
    for (int i = INVALID_TAB_ID; i < browser_window->GetRemainingTabAmount()-1; ++i)
        childhwnd = GetNextWindow(childhwnd, GW_HWNDPREV); // Current webview's hwnd, there is no better way to get that unfortunately
    return childhwnd;
}