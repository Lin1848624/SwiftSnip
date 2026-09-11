// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <windows.h>

#include <string>

// 将 HBITMAP 保存为 PNG 文件；失败时通过 errorOut 返回原因
bool SaveBitmapAsPng(HBITMAP bitmap, const std::wstring& path, std::wstring* errorOut);

