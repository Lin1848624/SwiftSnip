#pragma once

#include <windows.h>

// 捕获结果：位图及其左上角对应的屏幕坐标
struct CapturedImage {
    HBITMAP bitmap = nullptr;
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;

    bool Valid() const { return bitmap != nullptr; }
    void Release();
};

// 捕获整个虚拟屏幕（所有显示器，含负坐标区域）
bool CaptureVirtualScreen(CapturedImage* out);

// 捕获主显示器
bool CapturePrimaryMonitor(CapturedImage* out);

// 捕获屏幕上的任意矩形区域；rect 为虚拟屏幕物理像素坐标
bool CaptureScreenRegion(const RECT& rect, CapturedImage* out);

// 从已有位图中裁剪一块区域；rect 为相对位图左上角的坐标
bool CropCapturedImage(const CapturedImage& source, const RECT& rect, CapturedImage* out);

// 生成叠加 40% 黑色后的变暗副本，用作遮罩底图
HBITMAP CreateDarkenedCopy(HBITMAP source, int width, int height);
