// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

#include "settings.h"

// 注册两个全局热键；全部成功返回空串，否则返回失败热键的显示名
std::wstring RegisterAppHotkeys(HWND hwnd, const AppSettings& settings);

// 注销两个全局热键
void UnregisterAppHotkeys(HWND hwnd);

// 检测热键当前是否可用（临时注册后立即注销）
bool IsHotkeyAvailable(const HotkeyConfig& hotkey);

