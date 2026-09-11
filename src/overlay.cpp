#include "overlay.h"

#include "app.h"

#include <windowsx.h>

#include <string>

namespace {

// 拖拽模式
enum class DragMode {
    None,
    Creating,  // 正在新建选区
    Moving,    // 正在移动选区
    Resizing,  // 正在缩放选区
};

// 选区命中部位
enum class Handle {
    None,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
    Inside,
};

struct OverlayState {
    HWND hwnd = nullptr;
    CapturedImage fullScreen;      // 全屏位图（所有权在本模块）
    HDC sourceDc = nullptr;        // 全屏位图 DC
    HGDIOBJ oldSourceBitmap = nullptr;
    HDC blackDc = nullptr;         // 1x1 黑色位图 DC，用于叠加变暗
    HBITMAP blackBitmap = nullptr;
    HGDIOBJ oldBlackBitmap = nullptr;
    HDC bufferDc = nullptr;        // 脏区缓冲
    HBITMAP bufferBitmap = nullptr;
    HGDIOBJ oldBufferBitmap = nullptr;
    int bufferWidth = 0;
    int bufferHeight = 0;

    int width = 0;
    int height = 0;

    bool hasSelection = false;
    RECT selection = {};
    bool dragging = false;
    DragMode mode = DragMode::None;
    Handle handle = Handle::None;
    POINT dragOrigin = {};
    RECT dragStartRect = {};

    HFONT font = nullptr;
    bool cropImage = true;
    std::function<void(CapturedImage, const RegionSelection&)> onDone;
    bool finishing = false;
};

OverlayState g_overlay;

RECT NormalizeRect(POINT a, POINT b) {
    RECT rect = {};
    rect.left = a.x < b.x ? a.x : b.x;
    rect.top = a.y < b.y ? a.y : b.y;
    rect.right = a.x > b.x ? a.x : b.x;
    rect.bottom = a.y > b.y ? a.y : b.y;
    return rect;
}

bool IsUsableSelection(const RECT& rect) {
    return rect.right - rect.left >= 3 && rect.bottom - rect.top >= 3;
}

Handle HitTest(const OverlayState& state, POINT pt) {
    if (!state.hasSelection) {
        return Handle::None;
    }
    const RECT& rect = state.selection;
    const int centerX = (rect.left + rect.right) / 2;
    const int centerY = (rect.top + rect.bottom) / 2;
    const int half = 7;

    const struct {
        Handle handle;
        POINT point;
    } handles[] = {
        {Handle::TopLeft, {rect.left, rect.top}},
        {Handle::Top, {centerX, rect.top}},
        {Handle::TopRight, {rect.right, rect.top}},
        {Handle::Right, {rect.right, centerY}},
        {Handle::BottomRight, {rect.right, rect.bottom}},
        {Handle::Bottom, {centerX, rect.bottom}},
        {Handle::BottomLeft, {rect.left, rect.bottom}},
        {Handle::Left, {rect.left, centerY}},
    };
    for (const auto& item : handles) {
        if (abs(pt.x - item.point.x) <= half && abs(pt.y - item.point.y) <= half) {
            return item.handle;
        }
    }
    if (pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom) {
        return Handle::Inside;
    }
    return Handle::None;
}

bool IsResizeHandle(Handle handle) {
    return handle != Handle::None && handle != Handle::Inside;
}

LPCWSTR CursorForHandle(Handle handle) {
    switch (handle) {
        case Handle::TopLeft:
        case Handle::BottomRight:
            return IDC_SIZENWSE;
        case Handle::TopRight:
        case Handle::BottomLeft:
            return IDC_SIZENESW;
        case Handle::Top:
        case Handle::Bottom:
            return IDC_SIZENS;
        case Handle::Left:
        case Handle::Right:
            return IDC_SIZEWE;
        case Handle::Inside:
            return IDC_SIZEALL;
        default:
            return IDC_CROSS;
    }
}

void EnsureBuffer(OverlayState& state, int width, int height) {
    if (state.bufferDc != nullptr && state.bufferWidth >= width && state.bufferHeight >= height) {
        return;
    }
    if (state.bufferDc != nullptr) {
        SelectObject(state.bufferDc, state.oldBufferBitmap);
        DeleteDC(state.bufferDc);
        state.bufferDc = nullptr;
    }
    if (state.bufferBitmap != nullptr) {
        DeleteObject(state.bufferBitmap);
        state.bufferBitmap = nullptr;
    }

    HDC screenDc = GetDC(nullptr);
    state.bufferDc = CreateCompatibleDC(screenDc);
    state.bufferBitmap = CreateCompatibleBitmap(screenDc, width, height);
    state.oldBufferBitmap = SelectObject(state.bufferDc, state.bufferBitmap);
    ReleaseDC(nullptr, screenDc);
    state.bufferWidth = width;
    state.bufferHeight = height;
}

void DrawSizeLabel(HDC dc, const OverlayState& state, const RECT& selection, int offsetX, int offsetY) {
    const int width = selection.right - selection.left;
    const int height = selection.bottom - selection.top;
    const std::wstring text = std::to_wstring(width) + L" × " + std::to_wstring(height);

    SIZE textSize = {};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &textSize);
    const int boxWidth = textSize.cx + 14;
    const int boxHeight = textSize.cy + 8;

    int x = selection.left;
    int y = selection.bottom + 8;
    if (y + boxHeight > state.height) {
        y = selection.top - boxHeight - 8;
    }
    if (x + boxWidth > state.width) {
        x = state.width - boxWidth;
    }
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }

    RECT box = {x + offsetX, y + offsetY, x + boxWidth + offsetX, y + boxHeight + offsetY};
    FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, box.left + 7, box.top + 4, text.c_str(), static_cast<int>(text.size()));
}

void DrawSelectionDecorations(HDC dc, const OverlayState& state, const RECT& dirty) {
    const int offsetX = -dirty.left;
    const int offsetY = -dirty.top;
    RECT selection = state.selection;
    OffsetRect(&selection, offsetX, offsetY);

    // 选区边框
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, selection.left, selection.top, selection.right, selection.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    // 8 个手柄
    const int centerX = (selection.left + selection.right) / 2;
    const int centerY = (selection.top + selection.bottom) / 2;
    const int half = 5;
    const POINT points[] = {
        {selection.left, selection.top},     {centerX, selection.top},          {selection.right, selection.top},
        {selection.right, centerY},          {selection.right, selection.bottom}, {centerX, selection.bottom},
        {selection.left, selection.bottom},  {selection.left, centerY},
    };
    HPEN handlePen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
    HBRUSH handleBrush = CreateSolidBrush(RGB(255, 255, 255));
    for (const POINT& point : points) {
        RECT handleRect = {point.x - half, point.y - half, point.x + half + 1, point.y + half + 1};
        FillRect(dc, &handleRect, handleBrush);
        HGDIOBJ previousPen = SelectObject(dc, handlePen);
        HGDIOBJ previousBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, handleRect.left, handleRect.top, handleRect.right, handleRect.bottom);
        SelectObject(dc, previousBrush);
        SelectObject(dc, previousPen);
    }
    DeleteObject(handleBrush);
    DeleteObject(handlePen);

    DrawSizeLabel(dc, state, state.selection, offsetX, offsetY);
}

void DrawHint(HDC dc, const OverlayState& state, const RECT& dirty) {
    const wchar_t* hint =
        state.cropImage ? L"拖动鼠标选择区域　·　Enter 确认　·　Esc / 右键取消"
                        : L"拖动选择滚动区域　·　Enter 开始长截图　·　开始后按 Esc 或再按热键结束";
    SIZE textSize = {};
    GetTextExtentPoint32W(dc, hint, static_cast<int>(wcslen(hint)), &textSize);

    const int boxWidth = textSize.cx + 24;
    const int boxHeight = textSize.cy + 14;
    const int x = (state.width - boxWidth) / 2;
    const int y = 24;
    if (x < 0 || y + boxHeight > state.height) {
        return;
    }

    RECT box = {x - dirty.left, y - dirty.top, x + boxWidth - dirty.left, y + boxHeight - dirty.top};
    FillRect(dc, &box, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(240, 240, 240));
    TextOutW(dc, box.left + 12, box.top + 7, hint, static_cast<int>(wcslen(hint)));
}

void PaintOverlay(HWND hwnd) {
    OverlayState& state = g_overlay;

    PAINTSTRUCT ps = {};
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT dirty = ps.rcPaint;
    if (dirty.right <= dirty.left || dirty.bottom <= dirty.top || state.sourceDc == nullptr) {
        EndPaint(hwnd, &ps);
        return;
    }

    const int dirtyWidth = dirty.right - dirty.left;
    const int dirtyHeight = dirty.bottom - dirty.top;
    EnsureBuffer(state, dirtyWidth, dirtyHeight);
    HDC buffer = state.bufferDc;

    HGDIOBJ previousFont = nullptr;
    if (state.font != nullptr) {
        previousFont = SelectObject(buffer, state.font);
    }

    // 1. 源图脏区拷贝到缓冲
    BitBlt(buffer, 0, 0, dirtyWidth, dirtyHeight, state.sourceDc, dirty.left, dirty.top, SRCCOPY);

    // 2. 叠加 40% 黑色形成遮罩效果
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 102;
    AlphaBlend(buffer, 0, 0, dirtyWidth, dirtyHeight, state.blackDc, 0, 0, 1, 1, blend);

    // 3. 选区恢复原始亮度，并绘制装饰
    if (state.hasSelection) {
        RECT intersection = {};
        if (IntersectRect(&intersection, &dirty, &state.selection)) {
            BitBlt(buffer, intersection.left - dirty.left, intersection.top - dirty.top,
                   intersection.right - intersection.left, intersection.bottom - intersection.top, state.sourceDc,
                   intersection.left, intersection.top, SRCCOPY);
        }
        DrawSelectionDecorations(buffer, state, dirty);
    } else if (!state.dragging) {
        DrawHint(buffer, state, dirty);
    }

    // 4. 输出到窗口
    BitBlt(hdc, dirty.left, dirty.top, dirtyWidth, dirtyHeight, buffer, 0, 0, SRCCOPY);
    if (previousFont != nullptr) {
        SelectObject(buffer, previousFont);
    }
    EndPaint(hwnd, &ps);
}

// 计算需要重绘的区域：新旧选区的并集，外扩以覆盖手柄与尺寸标签
void InvalidateSelectionArea(HWND hwnd, const RECT& oldSelection, const RECT& newSelection, bool hasOld,
                             bool hasNew) {
    RECT region = {};
    bool valid = false;
    RECT temp = {};
    if (hasOld) {
        temp = oldSelection;
        InflateRect(&temp, 24, 24);
        temp.bottom += 40;  // 尺寸标签可能位于选区下方
        region = temp;
        valid = true;
    }
    if (hasNew) {
        temp = newSelection;
        InflateRect(&temp, 24, 24);
        temp.bottom += 40;
        if (valid) {
            UnionRect(&region, &region, &temp);
        } else {
            region = temp;
            valid = true;
        }
    }
    if (!valid) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return;
    }
    InvalidateRect(hwnd, &region, FALSE);
}

void UpdateSelection(OverlayState& state, POINT pt) {
    pt.x = pt.x < 0 ? 0 : (pt.x > state.width ? state.width : pt.x);
    pt.y = pt.y < 0 ? 0 : (pt.y > state.height ? state.height : pt.y);

    switch (state.mode) {
        case DragMode::Creating:
            state.selection = NormalizeRect(state.dragOrigin, pt);
            state.hasSelection = IsUsableSelection(state.selection);
            break;

        case DragMode::Moving: {
            RECT moved = state.dragStartRect;
            const int dx = pt.x - state.dragOrigin.x;
            const int dy = pt.y - state.dragOrigin.y;
            OffsetRect(&moved, dx, dy);
            if (moved.left < 0) {
                OffsetRect(&moved, -moved.left, 0);
            }
            if (moved.top < 0) {
                OffsetRect(&moved, 0, -moved.top);
            }
            if (moved.right > state.width) {
                OffsetRect(&moved, state.width - moved.right, 0);
            }
            if (moved.bottom > state.height) {
                OffsetRect(&moved, 0, state.height - moved.bottom);
            }
            state.selection = moved;
            break;
        }

        case DragMode::Resizing: {
            RECT rect = state.dragStartRect;
            switch (state.handle) {
                case Handle::TopLeft:
                    rect.left = pt.x;
                    rect.top = pt.y;
                    break;
                case Handle::Top:
                    rect.top = pt.y;
                    break;
                case Handle::TopRight:
                    rect.right = pt.x;
                    rect.top = pt.y;
                    break;
                case Handle::Right:
                    rect.right = pt.x;
                    break;
                case Handle::BottomRight:
                    rect.right = pt.x;
                    rect.bottom = pt.y;
                    break;
                case Handle::Bottom:
                    rect.bottom = pt.y;
                    break;
                case Handle::BottomLeft:
                    rect.left = pt.x;
                    rect.bottom = pt.y;
                    break;
                case Handle::Left:
                    rect.left = pt.x;
                    break;
                default:
                    break;
            }
            POINT a = {rect.left, rect.top};
            POINT b = {rect.right, rect.bottom};
            state.selection = NormalizeRect(a, b);
            state.hasSelection = IsUsableSelection(state.selection);
            break;
        }

        default:
            break;
    }
}

void ReleaseOverlayResources() {
    OverlayState& state = g_overlay;

    if (state.sourceDc != nullptr) {
        SelectObject(state.sourceDc, state.oldSourceBitmap);
        DeleteDC(state.sourceDc);
        state.sourceDc = nullptr;
    }
    if (state.blackDc != nullptr) {
        SelectObject(state.blackDc, state.oldBlackBitmap);
        DeleteDC(state.blackDc);
        state.blackDc = nullptr;
    }
    if (state.blackBitmap != nullptr) {
        DeleteObject(state.blackBitmap);
        state.blackBitmap = nullptr;
    }
    if (state.bufferDc != nullptr) {
        SelectObject(state.bufferDc, state.oldBufferBitmap);
        DeleteDC(state.bufferDc);
        state.bufferDc = nullptr;
    }
    if (state.bufferBitmap != nullptr) {
        DeleteObject(state.bufferBitmap);
        state.bufferBitmap = nullptr;
    }
    if (state.font != nullptr) {
        DeleteObject(state.font);
        state.font = nullptr;
    }

    state.fullScreen.Release();
    state.hasSelection = false;
    state.selection = {};
    state.dragging = false;
    state.mode = DragMode::None;
    state.handle = Handle::None;
    state.width = 0;
    state.height = 0;
    state.hwnd = nullptr;
    state.onDone = nullptr;
    state.cropImage = true;
    state.finishing = false;

    // 全屏位图等大块内存已释放，主动修剪工作集，让物理内存尽快归还系统
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
}

void FinishCapture(HWND hwnd, bool confirmed) {
    OverlayState& state = g_overlay;
    if (state.finishing) {
        return;
    }
    state.finishing = true;

    CapturedImage result;
    RegionSelection selection;
    if (confirmed && state.hasSelection && IsUsableSelection(state.selection)) {
        selection.bitmapRect = state.selection;
        selection.screenRect.left = state.fullScreen.originX + state.selection.left;
        selection.screenRect.top = state.fullScreen.originY + state.selection.top;
        selection.screenRect.right = state.fullScreen.originX + state.selection.right;
        selection.screenRect.bottom = state.fullScreen.originY + state.selection.bottom;

        if (state.cropImage) {
            // 全屏位图正被遮罩的源 DC 占用，先取消占用，否则裁剪时 SelectObject 会失败
            if (state.sourceDc != nullptr) {
                SelectObject(state.sourceDc, state.oldSourceBitmap);
            }
            CropCapturedImage(state.fullScreen, state.selection, &result);
        }
    }

    std::function<void(CapturedImage, const RegionSelection&)> callback = state.onDone;
    DestroyWindow(hwnd);
    if (callback) {
        callback(result, selection);
    } else if (result.Valid()) {
        result.Release();
    }
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    OverlayState& state = g_overlay;

    switch (message) {
        case WM_CREATE:
            state.hwnd = hwnd;
            SetFocus(hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;  // 双缓冲自绘，禁止擦背景避免闪烁

        case WM_PAINT:
            PaintOverlay(hwnd);
            return 0;

        case WM_SETCURSOR:
            if (LOWORD(lparam) == HTCLIENT) {
                POINT pt = {};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                SetCursor(LoadCursorW(nullptr, CursorForHandle(HitTest(state, pt))));
                return TRUE;
            }
            break;

        case WM_LBUTTONDOWN: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const Handle handle = HitTest(state, pt);
            const RECT oldSelection = state.selection;
            const bool hadSelection = state.hasSelection;

            state.dragOrigin = pt;
            state.dragStartRect = state.selection;
            if (IsResizeHandle(handle)) {
                state.mode = DragMode::Resizing;
                state.handle = handle;
            } else if (handle == Handle::Inside) {
                state.mode = DragMode::Moving;
                state.handle = Handle::Inside;
            } else {
                state.mode = DragMode::Creating;
                state.handle = Handle::None;
                state.hasSelection = false;
                state.selection = {};
            }
            state.dragging = true;
            SetCapture(hwnd);
            InvalidateSelectionArea(hwnd, oldSelection, state.selection, hadSelection, state.hasSelection);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (!state.dragging) {
                SetCursor(LoadCursorW(nullptr, CursorForHandle(HitTest(state, pt))));
                return 0;
            }
            const RECT oldSelection = state.selection;
            const bool hadSelection = state.hasSelection;
            UpdateSelection(state, pt);
            InvalidateSelectionArea(hwnd, oldSelection, state.selection, hadSelection, state.hasSelection);
            return 0;
        }

        case WM_LBUTTONUP: {
            if (!state.dragging) {
                return 0;
            }
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            if (state.mode == DragMode::Creating && !IsUsableSelection(state.selection)) {
                state.hasSelection = false;
                state.selection = {};
            }
            state.dragging = false;
            state.mode = DragMode::None;
            state.handle = Handle::None;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_RBUTTONUP:
            FinishCapture(hwnd, false);
            return 0;

        case WM_KEYDOWN:
            if (wparam == VK_RETURN) {
                FinishCapture(hwnd, true);
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                FinishCapture(hwnd, false);
                return 0;
            }
            break;

        case WM_DESTROY:
            if (GetCapture() == hwnd) {
                ReleaseCapture();
            }
            ReleaseOverlayResources();
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool EnsureWindowClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kOverlayWndClass;
    if (RegisterClassExW(&wc) == 0) {
        return false;
    }
    registered = true;
    return true;
}

}  // namespace

bool StartRegionCapture(HWND owner, CapturedImage fullScreen, bool cropImage,
                        std::function<void(CapturedImage, const RegionSelection&)> onDone) {
    if (!fullScreen.Valid() || g_overlay.hwnd != nullptr) {
        return false;
    }
    if (!EnsureWindowClass()) {
        return false;
    }

    OverlayState& state = g_overlay;
    state.fullScreen = fullScreen;
    state.width = fullScreen.width;
    state.height = fullScreen.height;
    state.cropImage = cropImage;
    state.onDone = std::move(onDone);
    state.finishing = false;

    HDC screenDc = GetDC(nullptr);
    state.sourceDc = CreateCompatibleDC(screenDc);
    state.oldSourceBitmap = SelectObject(state.sourceDc, state.fullScreen.bitmap);

    state.blackDc = CreateCompatibleDC(screenDc);
    state.blackBitmap = CreateCompatibleBitmap(screenDc, 1, 1);
    state.oldBlackBitmap = SelectObject(state.blackDc, state.blackBitmap);
    RECT onePixel = {0, 0, 1, 1};
    FillRect(state.blackDc, &onePixel, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    ReleaseDC(nullptr, screenDc);

    state.font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Microsoft YaHei UI");

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayWndClass, L"", WS_POPUP,
                                fullScreen.originX, fullScreen.originY, fullScreen.width, fullScreen.height, owner,
                                nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        ReleaseOverlayResources();
        return false;
    }

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

bool IsRegionCaptureActive() {
    return g_overlay.hwnd != nullptr;
}

void CancelRegionCapture() {
    if (g_overlay.hwnd != nullptr) {
        FinishCapture(g_overlay.hwnd, false);
    }
}
