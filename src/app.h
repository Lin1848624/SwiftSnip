// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

// 应用标识
inline constexpr wchar_t kAppName[] = L"瞬截";
inline constexpr wchar_t kAppId[] = L"SwiftSnip";
inline constexpr wchar_t kAppVersion[] = L"1.1.0";

// 窗口类名
inline constexpr wchar_t kMainWndClass[] = L"SwiftSnipMainWnd";
inline constexpr wchar_t kOverlayWndClass[] = L"SwiftSnipOverlayWnd";
inline constexpr wchar_t kSettingsWndClass[] = L"SwiftSnipSettingsWnd";

// 单实例互斥体与自定义消息
inline constexpr wchar_t kMutexName[] = L"SwiftSnip.SingleInstance.9C4B7F1A";
inline constexpr UINT WM_APP_TRAY = WM_APP + 1;
inline constexpr UINT WM_APP_OPEN_SETTINGS = WM_APP + 2;

// 全局热键 ID
inline constexpr int kHotkeyIdRegion = 1;
inline constexpr int kHotkeyIdFullscreen = 2;
inline constexpr int kHotkeyIdScroll = 3;

// 托盘菜单命令
inline constexpr int kCmdCaptureRegion = 1001;
inline constexpr int kCmdCaptureFullscreen = 1002;
inline constexpr int kCmdOpenSaveDir = 1003;
inline constexpr int kCmdOpenSettings = 1004;
inline constexpr int kCmdAbout = 1005;
inline constexpr int kCmdExit = 1006;
inline constexpr int kCmdCaptureScroll = 1007;

// 资源 ID
inline constexpr int kIconResourceId = 101;
