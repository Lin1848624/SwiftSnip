#include "capture.h"

namespace {

// 捕获屏幕上指定矩形区域（屏幕物理像素坐标）
bool CaptureScreenRectImpl(const RECT& rect, CapturedImage* out) {
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0 || out == nullptr) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return false;
    }

    HDC memoryDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    bool ok = false;
    if (memoryDc != nullptr && bitmap != nullptr) {
        HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
        ok = BitBlt(memoryDc, 0, 0, width, height, screenDc, rect.left, rect.top,
                    SRCCOPY | CAPTUREBLT) != FALSE;
        SelectObject(memoryDc, oldBitmap);
    }

    if (memoryDc != nullptr) {
        DeleteDC(memoryDc);
    }
    ReleaseDC(nullptr, screenDc);

    if (!ok) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        return false;
    }

    out->bitmap = bitmap;
    out->originX = rect.left;
    out->originY = rect.top;
    out->width = width;
    out->height = height;
    return true;
}

}  // namespace

void CapturedImage::Release() {
    if (bitmap != nullptr) {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }
    originX = 0;
    originY = 0;
    width = 0;
    height = 0;
}

bool CaptureVirtualScreen(CapturedImage* out) {
    RECT rect = {};
    rect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rect.right = rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rect.bottom = rect.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return CaptureScreenRectImpl(rect, out);
}

bool CapturePrimaryMonitor(CapturedImage* out) {
    POINT origin = {0, 0};
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
        return false;
    }
    return CaptureScreenRectImpl(info.rcMonitor, out);
}

bool CaptureScreenRegion(const RECT& rect, CapturedImage* out) {
    return CaptureScreenRectImpl(rect, out);
}

bool CropCapturedImage(const CapturedImage& source, const RECT& rect, CapturedImage* out) {
    if (!source.Valid() || out == nullptr) {
        return false;
    }

    RECT clipped = rect;
    clipped.left = max(clipped.left, 0L);
    clipped.top = max(clipped.top, 0L);
    clipped.right = min(clipped.right, static_cast<LONG>(source.width));
    clipped.bottom = min(clipped.bottom, static_cast<LONG>(source.height));

    const int width = clipped.right - clipped.left;
    const int height = clipped.bottom - clipped.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    // 使用屏幕 DC 创建彩色位图，避免内存 DC 默认单色位图的问题
    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return false;
    }
    HDC sourceDc = CreateCompatibleDC(screenDc);
    HDC targetDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);

    bool ok = false;
    if (sourceDc != nullptr && targetDc != nullptr && bitmap != nullptr) {
        HGDIOBJ oldSource = SelectObject(sourceDc, source.bitmap);
        HGDIOBJ oldTarget = SelectObject(targetDc, bitmap);
        // 位图若已被其他 DC 占用，SelectObject 会失败，此时必须报错而不是产出黑图
        if (oldSource != nullptr && oldSource != HGDI_ERROR && oldTarget != nullptr && oldTarget != HGDI_ERROR) {
            ok = BitBlt(targetDc, 0, 0, width, height, sourceDc, clipped.left, clipped.top, SRCCOPY) != FALSE;
        }
        if (oldTarget != nullptr && oldTarget != HGDI_ERROR) {
            SelectObject(targetDc, oldTarget);
        }
        if (oldSource != nullptr && oldSource != HGDI_ERROR) {
            SelectObject(sourceDc, oldSource);
        }
    }

    if (sourceDc != nullptr) {
        DeleteDC(sourceDc);
    }
    if (targetDc != nullptr) {
        DeleteDC(targetDc);
    }
    ReleaseDC(nullptr, screenDc);

    if (!ok) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        return false;
    }

    out->bitmap = bitmap;
    out->originX = source.originX + clipped.left;
    out->originY = source.originY + clipped.top;
    out->width = width;
    out->height = height;
    return true;
}

HBITMAP CreateDarkenedCopy(HBITMAP source, int width, int height) {
    if (source == nullptr || width <= 0 || height <= 0) {
        return nullptr;
    }

    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr) {
        return nullptr;
    }
    HDC sourceDc = CreateCompatibleDC(screenDc);
    HDC targetDc = CreateCompatibleDC(screenDc);
    HDC blackDc = CreateCompatibleDC(screenDc);
    HBITMAP target = nullptr;
    HBITMAP black = nullptr;

    if (sourceDc != nullptr && targetDc != nullptr && blackDc != nullptr) {
        target = CreateCompatibleBitmap(screenDc, width, height);
        black = CreateCompatibleBitmap(screenDc, 1, 1);
        if (target != nullptr && black != nullptr) {
            HGDIOBJ oldSource = SelectObject(sourceDc, source);
            HGDIOBJ oldTarget = SelectObject(targetDc, target);
            HGDIOBJ oldBlack = SelectObject(blackDc, black);

            RECT onePixel = {0, 0, 1, 1};
            FillRect(blackDc, &onePixel, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            BOOL copied = BitBlt(targetDc, 0, 0, width, height, sourceDc, 0, 0, SRCCOPY);
            // SourceConstantAlpha = 102 ≈ 40% 黑色叠加
            BLENDFUNCTION blend = {};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 102;
            BOOL blended = AlphaBlend(targetDc, 0, 0, width, height, blackDc, 0, 0, 1, 1, blend);

            SelectObject(blackDc, oldBlack);
            SelectObject(targetDc, oldTarget);
            SelectObject(sourceDc, oldSource);

            if (!copied || !blended) {
                DeleteObject(target);
                target = nullptr;
            }
        }
    }

    if (black != nullptr) {
        DeleteObject(black);
    }
    if (sourceDc != nullptr) {
        DeleteDC(sourceDc);
    }
    if (targetDc != nullptr) {
        DeleteDC(targetDc);
    }
    if (blackDc != nullptr) {
        DeleteDC(blackDc);
    }
    ReleaseDC(nullptr, screenDc);
    return target;
}
