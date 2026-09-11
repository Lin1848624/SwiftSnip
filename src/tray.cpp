// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tray.h"

#include "app.h"
#include "settings.h"

#include <cwchar>

namespace {

constexpr UINT kTrayIconId = 1;

NOTIFYICONDATAW g_nid = {};
bool g_installed = false;

// 优先进程资源图标，缺失时回退到系统默认图标
HICON LoadAppIcon() {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HICON icon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(kIconResourceId), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (icon == nullptr) {
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return icon;
}

}  // namespace

bool TrayInitialize(HWND hwnd) {
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayIconId;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = LoadAppIcon();
    wcscpy_s(g_nid.szTip, kAppName);

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        return false;
    }
    g_installed = true;
    return true;
}

void TrayShutdown() {
    if (!g_installed) {
        return;
    }
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_installed = false;
}

void TrayShowBalloon(const std::wstring& title, const std::wstring& text) {
    if (!g_installed) {
        return;
    }
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT TrayShowContextMenu(HWND hwnd) {
    const AppSettings& settings = Settings::Instance().Get();

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return 0;
    }

    const std::wstring regionText = L"区域截图(&A)\t" + HotkeyToString(settings.regionHotkey);
    const std::wstring scrollText = L"长截图(&L)\t" + HotkeyToString(settings.scrollHotkey);
    const std::wstring fullscreenText = L"全屏截图(&F)\t" + HotkeyToString(settings.fullscreenHotkey);
    AppendMenuW(menu, MF_STRING, kCmdCaptureRegion, regionText.c_str());
    AppendMenuW(menu, MF_STRING, kCmdCaptureScroll, scrollText.c_str());
    AppendMenuW(menu, MF_STRING, kCmdCaptureFullscreen, fullscreenText.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdOpenSaveDir, L"打开保存目录(&O)");
    AppendMenuW(menu, MF_STRING, kCmdOpenSettings, L"设置(&S)...");
    AppendMenuW(menu, MF_STRING, kCmdAbout, L"关于(&B)...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"退出(&X)");

    POINT pt = {};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    // 通知 shell 菜单已关闭，避免菜单残留
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return command;
}

void TraySetTooltip(const std::wstring& text) {
    if (!g_installed) {
        return;
    }
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
