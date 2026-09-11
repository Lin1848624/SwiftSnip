#pragma once

#include <windows.h>

#include <string>

// 创建托盘图标，成功返回 true
bool TrayInitialize(HWND hwnd);

// 移除托盘图标
void TrayShutdown();

// 弹出气泡提示
void TrayShowBalloon(const std::wstring& title, const std::wstring& text);

// 在鼠标位置弹出右键菜单，返回用户选中的命令 ID（未选择返回 0）
UINT TrayShowContextMenu(HWND hwnd);

// 更新托盘提示文本
void TraySetTooltip(const std::wstring& text);

