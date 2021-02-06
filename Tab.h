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

struct DockRatio // Of Webview, not the dev tools
{
    double XWidth, YHeight; // Range: 0.0f > x >= 1.0f and 1.0f means FULL
};

struct HASH
{
    template <typename T>
    std::size_t operator()(T t) const {
        return static_cast<std::size_t>(t);
    }
};

// Default window ratio which is used to calculate each DockData inside the DockDataMap
// This way you set an initial position and dimension for each DockState
// Below, the browser is made of 70% webview and what's left is dev tools which is 30%.
const std::unordered_map<DockState, DockRatio, HASH> DockRatioMap =
{
    //DS_UNDOCK inherits everything after the window created automatically
    {DockState::DS_DOCK_RIGHT, {0.7f, 1.0f}},
    {DockState::DS_DOCK_LEFT, {0.7f, 1.0f}},
    {DockState::DS_DOCK_BOTTOM, {1.0f, 0.7f}}
};

struct DockData
{
    DockData(int _X, int _Y, int _nWidth, int _nHeight) : X(_X), Y(_Y), nWidth(_nWidth), nHeight(_nHeight) {}
    int X, Y, nWidth, nHeight;
};

// Current window dimensions
static std::unordered_map<DockState, std::shared_ptr<DockData>, HASH> DockDataMap =
{
    {DockState::DS_DOCK_RIGHT, nullptr},
    {DockState::DS_DOCK_LEFT, nullptr},
    {DockState::DS_DOCK_BOTTOM, nullptr}
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
    void CalculateDockData(RECT bounds);
    HWND GetDevTools();
    HWND GetWindowHandle();
    DockState GetDevToolsState();
    size_t GetTabId() { return m_tabId; }
protected:
    HWND m_parentHWnd = nullptr;
    HWND hwnd_DevTools = nullptr;
    HWND m_hWnd = nullptr;
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
    DWORD possible_PID; // Possible PID of the DevTools
    DWORD pid_DevTools = 0;
    DockState DevToolsState;
};
