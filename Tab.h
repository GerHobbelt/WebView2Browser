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

static DockState operator+ (DockState const &d, int const &n) 
{
    return static_cast<DockState>(static_cast<int>(d)+n); 
}

class Tab
{
public:
    Tab();
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> m_contentController;
    Microsoft::WRL::ComPtr<ICoreWebView2> m_contentWebView;
    Microsoft::WRL::ComPtr<ICoreWebView2DevToolsProtocolEventReceiver> m_securityStateChangedReceiver;

    static std::unique_ptr<Tab> CreateNewTab(HWND hWnd, ICoreWebView2Environment* env, size_t id, bool shouldBeActive);
    HRESULT ResizeWebView(bool recalculate = false);
    void FindDevTools();
    HWND GetDevTools();
    HWND GetDevToolsHolder() { return m_devtHolderHWnd; }
    DockState GetDevToolsState();
    void SetDevToolsState(DockState ds) { DevToolsState = ds; }
    void DockDevTools(DockState state);
protected:
    HWND m_parentHWnd = nullptr;
    HWND m_devtHWnd = nullptr; // Dev tools window
    HWND m_devtHolderHWnd = nullptr; // Window that hosts dev tools window
    HWND m_HWnd = nullptr; // Webview window
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
private:
    static BOOL CALLBACK EnumWindowsProcStatic(_In_ HWND hwnd, _In_ LPARAM lParam);
    BOOL CALLBACK EnumWindowsProc(_In_ HWND hwnd, _In_ LPARAM lParam);
    static LRESULT CALLBACK dtWndProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    void CalculateDockData(RECT bounds);
    DockState DevToolsState;
    DWORD possible_PID; // Possible PID of the DevTools
    DWORD pid_DevTools = 0;
    int rzBorderSize = 4; // Border size of resizable side of m_devtHolderHWnd
    std::tuple<RECT, bool> tplRect = {{0,0,0,0}, false}; // Default border rect of m_devtHolderHWnd
    std::unordered_map<DockState, std::unique_ptr<DockData>, HASH> DockDataMap; // Current window dimensions
};
