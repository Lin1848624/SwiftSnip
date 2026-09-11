#pragma once

#include <windows.h>

#include <functional>

#include "capture.h"

// 开始区域选择。fullScreen 的所有权转移给遮罩窗口，结束后自动释放。
// onDone 在主线程中回调，参数为裁剪后的位图；位图为空表示用户取消。
// 回调返回后位图由调用方负责释放。
bool StartRegionCapture(HWND owner, CapturedImage fullScreen, std::function<void(CapturedImage)> onDone);

// 当前是否正在进行区域选择
bool IsRegionCaptureActive();

// 主动取消当前区域选择
void CancelRegionCapture();

