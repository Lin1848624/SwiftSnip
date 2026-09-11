// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scroll_capture.h"

#include "app.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {

constexpr UINT_PTR kTimerId = 0x5C01;
constexpr int kTimerIntervalMs = 40;
constexpr int kCursorSettleMs = 140;   // 光标移动后等待目标窗口获得滚轮焦点
constexpr int kRenderWaitMs = 380;     // 滚动后等待目标应用重绘
constexpr int kRetryWaitMs = 220;      // 画面无变化时的重试间隔
constexpr int kMaxNotches = 10;        // 单次滚动的最多滚轮格数
constexpr int kMaxFrames = 400;        // 帧数安全上限
constexpr int kNoChangeLimit = 2;      // 连续无变化次数达到该值判定到底
constexpr int kCalibrateRetryLimit = 3;
constexpr float kScrollRatio = 0.45f;  // 每次滚动选区高度的比例（留足重叠，保证模板可验证）
constexpr double kMatchScoreLimit = 12.0;  // 平均每通道差异上限，超过视为不可信
constexpr double kMinTemplateVariance = 40.0;  // 模板条带亮度方差下限，过滤纯色区域
constexpr int kShiftTolerance = 3;             // 多数表决允许的位移偏差（像素）
constexpr int kMatchFailLimit = 4;             // 连续匹配失败次数上限
constexpr double kAmbiguityMargin = 0.5;       // 最佳与次佳匹配分数差下限，低于该值视为内容重复、不可信
constexpr double kStaticThreshold = 0.5;       // 同位置帧差低于该值视为画面完全静止（已到底）

// 滚动期间显示的选区外框：完全位于选区之外，不会被截入长图
constexpr wchar_t kFrameWndClass[] = L"SwiftSnipScrollFrame";
constexpr int kFrameMargin = 4;

// 帧缓冲：32 位 top-down DIB，可直接访问像素用于匹配
struct Frame {
    HBITMAP bitmap = nullptr;
    void* bits = nullptr;
    int width = 0;
    int height = 0;
    HDC dc = nullptr;
    HGDIOBJ oldBitmap = nullptr;

    Frame() = default;
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    Frame(Frame&& other) noexcept { MoveFrom(other); }
    Frame& operator=(Frame&& other) noexcept {
        if (this != &other) {
            Release();
            MoveFrom(other);
        }
        return *this;
    }
    ~Frame() { Release(); }

    void MoveFrom(Frame& other) {
        bitmap = other.bitmap;
        bits = other.bits;
        width = other.width;
        height = other.height;
        dc = other.dc;
        oldBitmap = other.oldBitmap;
        other.bitmap = nullptr;
        other.bits = nullptr;
        other.width = 0;
        other.height = 0;
        other.dc = nullptr;
        other.oldBitmap = nullptr;
    }

    bool Create(int w, int h) {
        Release();
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr) {
            return false;
        }

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;  // 负值表示 top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HBITMAP created = CreateDIBSection(screenDc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        HDC memoryDc = CreateCompatibleDC(screenDc);
        ReleaseDC(nullptr, screenDc);

        if (created == nullptr || memoryDc == nullptr || pixels == nullptr) {
            if (created != nullptr) {
                DeleteObject(created);
            }
            if (memoryDc != nullptr) {
                DeleteDC(memoryDc);
            }
            return false;
        }

        bitmap = created;
        bits = pixels;
        width = w;
        height = h;
        dc = memoryDc;
        oldBitmap = SelectObject(dc, bitmap);
        return true;
    }

    void Release() {
        if (dc != nullptr) {
            if (oldBitmap != nullptr) {
                SelectObject(dc, oldBitmap);
            }
            DeleteDC(dc);
            dc = nullptr;
            oldBitmap = nullptr;
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
            bitmap = nullptr;
        }
        bits = nullptr;
        width = 0;
        height = 0;
    }

    bool CaptureFromScreen(const RECT& region) {
        HDC screenDc = GetDC(nullptr);
        if (screenDc == nullptr) {
            return false;
        }
        const BOOL ok = BitBlt(dc, 0, 0, width, height, screenDc, region.left, region.top, SRCCOPY | CAPTUREBLT);
        ReleaseDC(nullptr, screenDc);
        return ok != FALSE;
    }
};

struct MatchResult {
    int offset = -1;      // 在上一帧中匹配到当前帧顶部的行号，即滚动位移
    double score = 1e9;   // 平均每通道差异
};

struct StripMatch {
    int shift = -1;     // 相对模板位置的位移（像素）
    double score = 1e9; // 平均每通道差异
};

enum class SessionState {
    Idle,
    WaitAfterCursorMove,
    Calibrating,
    Scrolling,
};

struct Session {
    bool active = false;
    bool stopRequested = false;
    HWND owner = nullptr;
    RECT region = {};
    int regionWidth = 0;
    int regionHeight = 0;
    int maxHeight = 20000;

    Frame frames[2];
    int frameIndex = 0;

    Frame stitch;
    int stitchCapacity = 0;
    int stitchHeight = 0;

    int unitOffset = 0;
    int notches = 1;
    int noChangeCount = 0;
    int matchFailCount = 0;
    int calibrateRetry = 0;
    int frameCount = 0;

    POINT savedCursor = {};
    SessionState state = SessionState::Idle;
    ULONGLONG waitUntil = 0;
    std::function<void(CapturedImage)> onDone;
    HHOOK hook = nullptr;
    HWND frameWindow = nullptr;
};

Session g_session;

LRESULT CALLBACK ScrollFrameProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;  // 点击穿透，不干扰用户操作
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 创建选区外框窗口（仅显示滚动范围与进行状态，不遮挡任何被截内容）
HWND CreateScrollFrameWindow(const RECT& region) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ScrollFrameProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(0, 120, 215));
        wc.lpszClassName = kFrameWndClass;
        if (RegisterClassExW(&wc) == 0) {
            return nullptr;
        }
        registered = true;
    }

    const int x = region.left - kFrameMargin;
    const int y = region.top - kFrameMargin;
    const int width = (region.right - region.left) + kFrameMargin * 2;
    const int height = (region.bottom - region.top) + kFrameMargin * 2;
    if (width <= 0 || height <= 0) {
        return nullptr;
    }

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                                kFrameWndClass, L"", WS_POPUP, x, y, width, height, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        return nullptr;
    }

    // 挖空中间区域，只保留选区外侧的细边框
    HRGN outer = CreateRectRgn(0, 0, width, height);
    HRGN inner = CreateRectRgn(kFrameMargin, kFrameMargin, width - kFrameMargin, height - kFrameMargin);
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    SetWindowRgn(hwnd, outer, TRUE);  // 区域所有权交给系统

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return hwnd;
}

// 计算模板条带的亮度方差，用于过滤纯色/低纹理区域
double ComputeLumaVariance(const Frame& frame, int top, int stripHeight) {
    const int stride = frame.width * 4;
    const BYTE* bits = static_cast<const BYTE*>(frame.bits);
    int stepX = frame.width / 80;
    if (stepX < 1) {
        stepX = 1;
    }
    const int stepY = 3;

    double sum = 0;
    double sumSquares = 0;
    int count = 0;
    for (int row = 0; row < stripHeight; row += stepY) {
        const BYTE* line = bits + static_cast<size_t>(top + row) * stride;
        for (int x = 0; x < frame.width; x += stepX) {
            const BYTE* pixel = line + static_cast<size_t>(x) * 4;
            const double luma = 0.114 * pixel[0] + 0.587 * pixel[1] + 0.299 * pixel[2];
            sum += luma;
            sumSquares += luma * luma;
            ++count;
        }
    }
    if (count == 0) {
        return 0;
    }
    const double mean = sum / count;
    const double variance = sumSquares / count - mean * mean;
    return variance > 0 ? variance : 0;
}

// 计算模板条带在 previous 中偏移 shift 处的平均每通道差异
double ComputeSadAt(const Frame& previous, const Frame& current, int top, int stripHeight, int shift, int stepX,
                    int stepY) {
    const int width = current.width;
    const int height = current.height;
    const int targetTop = top + shift;
    if (targetTop < 0 || targetTop + stripHeight > height) {
        return 1e9;
    }

    const int stride = width * 4;
    const BYTE* currentBits = static_cast<const BYTE*>(current.bits);
    const BYTE* previousBits = static_cast<const BYTE*>(previous.bits);

    unsigned long long sad = 0;
    int count = 0;
    for (int row = 0; row < stripHeight; row += stepY) {
        const BYTE* a = currentBits + static_cast<size_t>(top + row) * stride;
        const BYTE* b = previousBits + static_cast<size_t>(targetTop + row) * stride;
        for (int x = 0; x < width; x += stepX) {
            const BYTE* pa = a + static_cast<size_t>(x) * 4;
            const BYTE* pb = b + static_cast<size_t>(x) * 4;
            sad += abs(pa[0] - pb[0]) + abs(pa[1] - pb[1]) + abs(pa[2] - pb[2]);
            count += 3;
        }
    }
    return count > 0 ? static_cast<double>(sad) / count : 1e9;
}

// 在上一帧中搜索当前帧 [top, top+stripHeight) 条带的最匹配位移。
// 两阶段搜索：先稀疏粗搜定位，再在候选附近密集精搜，并做唯一性检查。
StripMatch MatchStripAt(const Frame& previous, const Frame& current, int top, int stripHeight, int minShift,
                        int maxShift) {
    StripMatch result;

    // 阶段一：粗搜（大步长采样 + 4 像素搜索步进）
    int coarseShift = -1;
    double coarseScore = 1e9;
    for (int shift = minShift; shift <= maxShift; shift += 4) {
        const double score = ComputeSadAt(previous, current, top, stripHeight, shift, 16, 6);
        if (score < coarseScore) {
            coarseScore = score;
            coarseShift = shift;
        }
    }
    if (coarseShift < 0) {
        return result;
    }

    // 阶段二：在粗搜结果附近精搜
    int bestShift = -1;
    double bestScore = 1e9;
    double secondScore = 1e9;
    const int from = coarseShift - 8;
    const int to = coarseShift + 8;
    for (int shift = from; shift <= to; ++shift) {
        if (shift < minShift || shift > maxShift) {
            continue;
        }
        const double score = ComputeSadAt(previous, current, top, stripHeight, shift, 4, 2);
        if (score < bestScore) {
            if (bestShift >= 0 && abs(shift - bestShift) > kShiftTolerance) {
                secondScore = bestScore;
            }
            bestScore = score;
            bestShift = shift;
        } else if (score < secondScore && (bestShift < 0 || abs(shift - bestShift) > kShiftTolerance)) {
            secondScore = score;
        }
    }

    if (bestShift < 0) {
        return result;
    }
    // 唯一性检查：若存在几乎同样好的其他位置，说明内容在该尺度上重复，位移不可信
    if (secondScore < 1e9 && secondScore - bestScore < kAmbiguityMargin) {
        return result;
    }
    result.shift = bestShift;
    result.score = bestScore;
    return result;
}

// 多候选条带匹配 + 多数表决：
// 在选区内取多个位置的条带分别匹配，固定表头、浮动按钮等局部元素会给出少数派结果而被淘汰
MatchResult MultiMatch(const Frame& previous, const Frame& current, int minShift, int maxShift) {
    MatchResult result;
    if (previous.bits == nullptr || current.bits == nullptr) {
        return result;
    }

    const int height = current.height;
    if (maxShift < minShift) {
        return result;
    }

    // 模板高度取选区高度的 1/6，并保证"模板位置 + 最大位移"仍落在上一帧内，
    // 否则该模板在上一帧中不可见，会产生假匹配
    const int topMin = height * 5 / 100;  // 从 5% 高度开始，尽量避开顶部固定表头
    int stripHeight = height / 6;
    if (stripHeight > 160) {
        stripHeight = 160;
    }
    int topMax = height - stripHeight - maxShift;
    if (topMax < topMin) {
        stripHeight = height - topMin - maxShift;
        if (stripHeight < 16) {
            return result;  // 位移过大，找不到可验证的模板位置
        }
        topMax = height - stripHeight - maxShift;
    }
    if (topMax < topMin) {
        return result;
    }

    struct Hit {
        int shift;
        double score;
    };
    std::vector<Hit> hits;

    constexpr int kCandidateCount = 4;
    for (int index = 0; index < kCandidateCount; ++index) {
        const int top = topMin + (topMax - topMin) * index / (kCandidateCount - 1);
        const double variance = ComputeLumaVariance(current, top, stripHeight);
        if (variance < kMinTemplateVariance) {
            continue;  // 纯色区域没有匹配特征
        }
        const StripMatch match = MatchStripAt(previous, current, top, stripHeight, minShift, maxShift);
        if (match.shift >= 0 && match.score <= kMatchScoreLimit) {
            hits.push_back({match.shift, match.score});
        }
    }

    if (hits.empty()) {
        return result;
    }
    if (hits.size() == 1) {
        result.offset = hits[0].shift;
        result.score = hits[0].score;
        return result;
    }

    // 找出最大的位移一致簇
    int bestCount = 0;
    long long bestSum = 0;
    double bestScore = 1e9;
    for (const Hit& seed : hits) {
        int count = 0;
        long long sum = 0;
        double scoreSum = 0;
        for (const Hit& other : hits) {
            if (abs(other.shift - seed.shift) <= kShiftTolerance) {
                ++count;
                sum += other.shift;
                scoreSum += other.score;
            }
        }
        const double averageScore = scoreSum / count;
        if (count > bestCount || (count == bestCount && averageScore < bestScore)) {
            bestCount = count;
            bestSum = sum;
            bestScore = averageScore;
        }
    }

    if (bestCount < 2) {
        // 无共识时退而求其次：取分数最低且明显优于次低的候选
        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score < b.score; });
        if (hits.size() >= 2 && hits[0].score < hits[1].score * 0.8) {
            result.offset = hits[0].shift;
            result.score = hits[0].score;
            return result;
        }
        return result;
    }
    result.offset = static_cast<int>(bestSum / bestCount);
    result.score = bestScore;
    return result;
}

void SendWheel(int notches) {
    if (notches < 1) {
        return;
    }
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    // 负值表示向下滚动
    input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA * notches);
    SendInput(1, &input, sizeof(INPUT));
}

void CleanupFrames(Session& session) {
    session.frames[0].Release();
    session.frames[1].Release();
    session.stitch.Release();
    session.stitchCapacity = 0;
    session.stitchHeight = 0;
}

void CleanupSession(Session& session) {
    if (session.owner != nullptr) {
        KillTimer(session.owner, kTimerId);
    }
    if (session.hook != nullptr) {
        UnhookWindowsHookEx(session.hook);
        session.hook = nullptr;
    }
    if (session.frameWindow != nullptr) {
        DestroyWindow(session.frameWindow);
        session.frameWindow = nullptr;
    }
    CleanupFrames(session);
    session.active = false;
    session.stopRequested = false;
    session.owner = nullptr;
    session.onDone = nullptr;
    session.state = SessionState::Idle;
    session.frameIndex = 0;
    session.unitOffset = 0;
    session.notches = 1;
    session.noChangeCount = 0;
    session.matchFailCount = 0;
    session.calibrateRetry = 0;
    session.frameCount = 0;

    // 释放长图缓冲后修剪工作集
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
}

// 扩容拼接缓冲，保留已有内容
bool GrowStitch(Session& session, int requiredHeight) {
    if (session.stitchCapacity >= requiredHeight) {
        return true;
    }
    int newCapacity = session.stitchCapacity + session.stitchCapacity / 2;
    if (newCapacity < requiredHeight) {
        newCapacity = requiredHeight;
    }
    if (newCapacity > session.maxHeight) {
        newCapacity = session.maxHeight;
    }
    if (newCapacity < requiredHeight) {
        return false;
    }

    Frame bigger;
    if (!bigger.Create(session.regionWidth, newCapacity)) {
        return false;
    }
    if (session.stitch.bitmap != nullptr && session.stitchHeight > 0) {
        BitBlt(bigger.dc, 0, 0, session.regionWidth, session.stitchHeight, session.stitch.dc, 0, 0, SRCCOPY);
    }
    session.stitch = std::move(bigger);
    session.stitchCapacity = newCapacity;
    return true;
}

// 把当前帧的底部 offset 行追加到长图缓冲
bool AppendFrame(Session& session, const Frame& frame, int offset) {
    if (offset <= 0) {
        return true;
    }
    const int rows = offset > frame.height ? frame.height : offset;
    const int sourceY = frame.height - rows;
    if (!GrowStitch(session, session.stitchHeight + rows)) {
        return false;
    }
    BitBlt(session.stitch.dc, 0, session.stitchHeight, frame.width, rows, frame.dc, 0, sourceY, SRCCOPY);
    session.stitchHeight += rows;
    return true;
}

CapturedImage FinalizeImage(Session& session) {
    CapturedImage image;
    if (session.stitchHeight <= session.regionHeight || session.stitch.bitmap == nullptr) {
        return image;
    }

    Frame finalFrame;
    if (!finalFrame.Create(session.regionWidth, session.stitchHeight)) {
        return image;
    }
    BitBlt(finalFrame.dc, 0, 0, session.regionWidth, session.stitchHeight, session.stitch.dc, 0, 0, SRCCOPY);

    // 交给会话外部的调用方，需要脱离 finalFrame 生命周期
    HBITMAP bitmap = finalFrame.bitmap;
    finalFrame.bitmap = nullptr;
    if (finalFrame.dc != nullptr) {
        SelectObject(finalFrame.dc, finalFrame.oldBitmap);
        DeleteDC(finalFrame.dc);
        finalFrame.dc = nullptr;
        finalFrame.oldBitmap = nullptr;
    }

    image.bitmap = bitmap;
    image.originX = 0;
    image.originY = 0;
    image.width = session.regionWidth;
    image.height = session.stitchHeight;
    return image;
}

void Finish(bool /*keepPartial*/) {
    Session& session = g_session;
    if (!session.active) {
        return;
    }

    CapturedImage image = FinalizeImage(session);
    std::function<void(CapturedImage)> callback = session.onDone;

    // 恢复用户原来的光标位置
    SetCursorPos(session.savedCursor.x, session.savedCursor.y);
    CleanupSession(session);

    if (callback) {
        callback(image);
    } else if (image.Valid()) {
        image.Release();
    }
}

void CaptureFirstFrameStage() {
    Session& session = g_session;
    if (!session.frames[0].CaptureFromScreen(session.region)) {
        Finish(false);
        return;
    }
    if (!GrowStitch(session, session.regionHeight)) {
        Finish(false);
        return;
    }
    BitBlt(session.stitch.dc, 0, 0, session.regionWidth, session.regionHeight, session.frames[0].dc, 0, 0, SRCCOPY);
    session.stitchHeight = session.regionHeight;
    session.frameIndex = 0;
    session.state = SessionState::Calibrating;
    SendWheel(1);
    session.waitUntil = GetTickCount64() + kRenderWaitMs;
}

void CalibrateStage() {
    Session& session = g_session;
    Frame& previous = session.frames[session.frameIndex];
    Frame& current = session.frames[1 - session.frameIndex];

    if (!current.CaptureFromScreen(session.region)) {
        Finish(false);
        return;
    }

    const MatchResult match = MultiMatch(previous, current, 0, session.regionHeight * 4 / 10);
    if (match.offset > 0) {
        session.unitOffset = match.offset;
        int target = static_cast<int>(session.regionHeight * kScrollRatio);
        if (target < 1) {
            target = 1;
        }
        int notches = target / session.unitOffset;
        if (notches < 1) {
            notches = 1;
        }
        if (notches > kMaxNotches) {
            notches = kMaxNotches;
        }
        session.notches = notches;
        session.frameIndex = 1 - session.frameIndex;
        session.stitchHeight = session.regionHeight;  // 第一帧已写入
        // 当前帧与上一帧的重叠部分即真实滚动量，先追加当前帧新增部分
        if (!AppendFrame(session, current, match.offset)) {
            Finish(true);
            return;
        }
        session.state = SessionState::Scrolling;
        session.noChangeCount = 0;
        SendWheel(notches);
        session.waitUntil = GetTickCount64() + kRenderWaitMs;
        return;
    }

    // 画面未变化：可能是渲染延迟，重试若干次后判定为不可滚动
    if (++session.calibrateRetry >= kCalibrateRetryLimit) {
        Finish(false);
        return;
    }
    SendWheel(1);
    session.waitUntil = GetTickCount64() + kRenderWaitMs + kRetryWaitMs;
}

void ScrollStage() {
    Session& session = g_session;
    Frame& previous = session.frames[session.frameIndex];
    Frame& current = session.frames[1 - session.frameIndex];

    if (!current.CaptureFromScreen(session.region)) {
        Finish(true);
        return;
    }

    ++session.frameCount;
    if (session.frameCount > kMaxFrames) {
        Finish(true);
        return;
    }

    const int expected = session.unitOffset * session.notches;
    // 搜索范围必须包含 0：页面停止滚动时匹配应回到 0，而不是在范围边界产生假位移
    const int minShift = 0;
    const int maxShift = expected * 115 / 100;

    // 快速静止检测：画面与上一帧完全一致（页面已滚动到底），直接判定无位移
    const double staticSad =
        ComputeSadAt(previous, current, current.height / 10, current.height * 8 / 10, 0, 8, 4);
    if (staticSad < kStaticThreshold) {
        if (++session.noChangeCount >= kNoChangeLimit) {
            Finish(true);
            return;
        }
        session.waitUntil = GetTickCount64() + kRetryWaitMs;
        return;
    }
    session.noChangeCount = 0;

    const MatchResult match = MultiMatch(previous, current, minShift, maxShift);

    if (match.offset < 0) {
        // 匹配失败（低纹理或候选无共识）：重试若干次后结束并保存已捕获内容
        if (++session.matchFailCount >= kMatchFailLimit) {
            Finish(true);
            return;
        }
        session.waitUntil = GetTickCount64() + kRetryWaitMs;
        return;
    }
    session.matchFailCount = 0;

    if (match.offset == 0) {
        // 画面确实没有位移：可能已滚动到底
        if (++session.noChangeCount >= kNoChangeLimit) {
            Finish(true);
            return;
        }
        session.waitUntil = GetTickCount64() + kRetryWaitMs;
        return;
    }

    session.noChangeCount = 0;
    if (!AppendFrame(session, current, match.offset)) {
        Finish(true);
        return;
    }

    session.frameIndex = 1 - session.frameIndex;
    if (session.stitchHeight >= session.maxHeight) {
        Finish(true);
        return;
    }

    SendWheel(session.notches);
    session.waitUntil = GetTickCount64() + kRenderWaitMs;
}

void OnTimerTick() {
    Session& session = g_session;
    if (!session.active) {
        return;
    }
    if (session.stopRequested) {
        Finish(true);
        return;
    }
    if (GetTickCount64() < session.waitUntil) {
        return;
    }

    switch (session.state) {
        case SessionState::WaitAfterCursorMove:
            CaptureFirstFrameStage();
            break;
        case SessionState::Calibrating:
            CalibrateStage();
            break;
        case SessionState::Scrolling:
            ScrollStage();
            break;
        default:
            break;
    }
}

// 低级键盘钩子：滚动期间按 Esc 结束（并吞掉该按键，避免影响目标应用）
LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_session.active) {
        const KBDLLHOOKSTRUCT* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        if ((wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) && info->vkCode == VK_ESCAPE) {
            g_session.stopRequested = true;
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

}  // namespace

bool StartScrollCapture(HWND owner, const ScrollCaptureOptions& options, std::function<void(CapturedImage)> onDone) {
    if (g_session.active || owner == nullptr) {
        return false;
    }

    const int width = options.region.right - options.region.left;
    const int height = options.region.bottom - options.region.top;
    if (width < 8 || height < 8) {
        return false;
    }

    Session& session = g_session;
    CleanupFrames(session);
    if (session.frameWindow != nullptr) {
        DestroyWindow(session.frameWindow);
        session.frameWindow = nullptr;
    }

    if (!session.frames[0].Create(width, height) || !session.frames[1].Create(width, height)) {
        CleanupFrames(session);
        return false;
    }

    session.active = true;
    session.stopRequested = false;
    session.owner = owner;
    session.region = options.region;
    session.regionWidth = width;
    session.regionHeight = height;
    session.maxHeight = options.maxHeight < height * 2 ? height * 2 : options.maxHeight;
    session.frameIndex = 0;
    session.unitOffset = 0;
    session.notches = 1;
    session.noChangeCount = 0;
    session.matchFailCount = 0;
    session.calibrateRetry = 0;
    session.frameCount = 0;
    session.stitchHeight = 0;
    session.stitchCapacity = 0;
    session.onDone = std::move(onDone);
    session.state = SessionState::WaitAfterCursorMove;
    session.waitUntil = GetTickCount64() + kCursorSettleMs;

    GetCursorPos(&session.savedCursor);
    SetCursorPos((options.region.left + options.region.right) / 2, (options.region.top + options.region.bottom) / 2);

    session.hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    session.frameWindow = CreateScrollFrameWindow(options.region);
    SetTimer(owner, kTimerId, kTimerIntervalMs, nullptr);
    return true;
}

bool IsScrollCaptureActive() {
    return g_session.active;
}

void StopScrollCapture() {
    if (g_session.active) {
        g_session.stopRequested = true;
    }
}

void CancelScrollCapture() {
    if (!g_session.active) {
        return;
    }
    Session& session = g_session;
    session.onDone = nullptr;
    Finish(false);
}

void ScrollCaptureHandleTimerMessage() {
    OnTimerTick();
}
