#pragma once

#include <windows.h>

// 打开或激活设置窗口
void ShowSettingsWindow(HWND owner);

// 设置窗口是否已打开
bool IsSettingsWindowOpen();

// 关闭设置窗口（程序退出前调用）
void CloseSettingsWindow();

