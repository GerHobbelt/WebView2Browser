// Copyright (C) Microsoft Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "framework.h"

enum class DockState: int
{
    DS_UNKNOWN, // No window or unable to locate
    DS_UNDOCK,
    DS_DOCK_RIGHT,
    DS_DOCK_LEFT,
    DS_DOCK_BOTTOM,
    DS_AMOUNT
};

struct Dimensions // Of Webview, not the dev tools
{
    float Width, Height; // 1.0f means FULL
};

struct HASH
{
    template <typename T>
    std::size_t operator()(T t) const {
        return static_cast<std::size_t>(t);
    }
};

const std::unordered_map<DockState, Dimensions, HASH> DockData =
{
    {DockState::DS_DOCK_RIGHT, {0.7f, 1.0f}}, // Example: Width of the browser is filled with 70% webview, 30% dev tools.
    {DockState::DS_DOCK_LEFT, {0.7f, 1.0f}},
    {DockState::DS_DOCK_BOTTOM, {1.0f, 0.7f}}
};

static DockState operator+ (DockState const &d1, int const &n) 
{
    return static_cast<DockState>(static_cast<int>(d1)+n); 
}

class Tab
{
public:
    Tab();
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_contentController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_contentWebView;
    Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> m_securityStateChangedReceiver;

    static BOOL CALLBACK EnumWindowsProcStatic(_In_ HWND hwnd, _In_ LPARAM lParam);
    BOOL CALLBACK EnumWindowsProc(_In_ HWND hwnd, _In_ LPARAM lParam);

    static std::unique_ptr<Tab> CreateNewTab(HWND hWnd, ICoreWebView2Environment* env, size_t id, bool shouldBeActive);
    HRESULT ResizeWebView();
    void DockDevTools(DockState state);
    HWND GetDevTools();
    HWND GetWindowHandle();
    DockState GetDevToolsState();
    size_t GetTabId() { return m_tabId; }
protected:
    HWND m_parentHWnd = nullptr;
    HWND hwnd_DevTools = nullptr;
    size_t m_tabId = INVALID_TAB_ID;
    EventRegistrationToken m_historyUpdateForwarderToken = {};
    EventRegistrationToken m_uriUpdateForwarderToken = {};
    EventRegistrationToken m_navStartingToken = {};
    EventRegistrationToken m_navCompletedToken = {};
    EventRegistrationToken m_securityUpdateToken = {};
    EventRegistrationToken m_messageBrokerToken = {};  // Message broker for browser pages loaded in a tab
    EventRegistrationToken m_acceleratorKeyPressedToken = {};
    Microsoft::WRL::ComPtr<ICoreWebView2WebMessageReceivedEventHandler> m_messageBroker;

    HRESULT Init(ICoreWebView2Environment* env, bool shouldBeActive);
    void SetMessageBroker();
    void FindDevTools();
    DWORD possible_PID; // Possibile PID of the DevTools
    DWORD pid_DevTools;
    DockState DevToolsState;
    RECT prevRect;
};
