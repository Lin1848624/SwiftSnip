#include "scroll_capture.h"

#include "app.h"

#include <cstdlib>
#include <utility>

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
constexpr float kScrollRatio = 0.65f;  // 每次滚动选区高度的比例
constexpr double kMatchScoreLimit = 32.0;  // 平均每通道差异上限，超过视为不可信

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
    int calibrateRetry = 0;
    int frameCount = 0;

    POINT savedCursor = {};
    SessionState state = SessionState::Idle;
    ULONGLONG waitUntil = 0;
    std::function<void(CapturedImage)> onDone;
    HHOOK hook = nullptr;
};

Session g_session;

// 在上一帧中搜索与当前帧顶部条带最匹配的位置
MatchResult MatchScrollOffset(const Frame& previous, const Frame& current, int searchMin, int searchMax) {
    MatchResult result;
    if (previous.bits == nullptr || current.bits == nullptr) {
        return result;
    }

    const int width = current.width;
    const int height = current.height;
    int stripHeight = height / 3;
    if (stripHeight > 64) {
        stripHeight = 64;
    }
    if (stripHeight <= 0) {
        return result;
    }

    int minY = searchMin < 0 ? 0 : searchMin;
    int maxY = searchMax;
    if (maxY > height - stripHeight) {
        maxY = height - stripHeight;
    }
    if (maxY < minY) {
        return result;
    }

    const int stride = width * 4;
    const BYTE* currentBits = static_cast<const BYTE*>(current.bits);
    const BYTE* previousBits = static_cast<const BYTE*>(previous.bits);
    int stepX = width / 160;
    if (stepX < 1) {
        stepX = 1;
    }
    const int stepY = 3;

    for (int y = minY; y <= maxY; ++y) {
        unsigned long long sad = 0;
        int count = 0;
        for (int row = 0; row < stripHeight; row += stepY) {
            const BYTE* a = currentBits + static_cast<size_t>(row) * stride;
            const BYTE* b = previousBits + static_cast<size_t>(y + row) * stride;
            for (int x = 0; x < width; x += stepX) {
                const BYTE* pa = a + static_cast<size_t>(x) * 4;
                const BYTE* pb = b + static_cast<size_t>(x) * 4;
                sad += abs(pa[0] - pb[0]) + abs(pa[1] - pb[1]) + abs(pa[2] - pb[2]);
                count += 3;
            }
        }
        if (count == 0) {
            continue;
        }
        const double score = static_cast<double>(sad) / count;
        if (score < result.score) {
            result.score = score;
            result.offset = y;
        }
    }
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

    const MatchResult match = MatchScrollOffset(previous, current, 1, session.regionHeight * 8 / 10);
    if (match.offset > 0 && match.score <= kMatchScoreLimit) {
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
    const int minY = expected * 2 / 5;
    const int maxY = expected * 8 / 5;
    const MatchResult match = MatchScrollOffset(previous, current, minY, maxY);

    if (match.offset <= 0 || match.score > kMatchScoreLimit * 2.0) {
        if (++session.noChangeCount >= kNoChangeLimit) {
            Finish(true);
            return;
        }
        // 再等一会儿重新捕获同一位置
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
