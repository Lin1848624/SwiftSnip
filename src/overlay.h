#pragma once

#include <windows.h>

#include <functional>

#include "capture.h"

// 选区结果：同时提供位图坐标与虚拟屏幕坐标
struct RegionSelection {
    RECT bitmapRect = {};
    RECT screenRect = {};
};

// 开始区域选择。fullScreen 的所有权转移给遮罩窗口，结束后自动释放。
// cropImage 为 true 时回调携带裁剪位图（普通截图）；为 false 时只提供选区坐标（长截图等场景）。
// onDone 在主线程中回调；位图为空表示用户取消或未选择有效区域。
// 回调返回后位图由调用方负责释放。
bool StartRegionCapture(HWND owner, CapturedImage fullScreen, bool cropImage,
                        std::function<void(CapturedImage, const RegionSelection&)> onDone);

// 当前是否正在进行区域选择
bool IsRegionCaptureActive();

// 主动取消当前区域选择
void CancelRegionCapture();
