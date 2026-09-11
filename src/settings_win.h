// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

// 打开或激活设置窗口
void ShowSettingsWindow(HWND owner);

// 设置窗口是否已打开
bool IsSettingsWindowOpen();

// 关闭设置窗口（程序退出前调用）
void CloseSettingsWindow();

