#pragma once

#include <windows.h>

#include <functional>

#include "capture.h"

// 长截图参数
struct ScrollCaptureOptions {
    RECT region = {};       // 选区（虚拟屏幕物理像素坐标）
    int maxHeight = 20000;  // 长图高度上限（像素）
};

// 开始长截图：自动滚动目标窗口并按帧拼接。
// onDone 在主线程回调；参数为拼接好的长图，位图为空表示未捕获到可滚动内容。
bool StartScrollCapture(HWND owner, const ScrollCaptureOptions& options, std::function<void(CapturedImage)> onDone);

// 当前是否有长截图会话在进行
bool IsScrollCaptureActive();

// 请求停止：保存已捕获内容后结束（用于再次按热键）
void StopScrollCapture();

// 立即取消且不保存（用于程序退出）
void CancelScrollCapture();

// 主窗口收到 WM_TIMER 时调用，驱动滚动状态机
void ScrollCaptureHandleTimerMessage();
