// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// 查询开机自启是否已开启（且与当前 exe 路径一致）
bool AutoStartIsEnabled();

// 设置开机自启
bool AutoStartSet(bool enabled);

