#include "hotkey.h"

#include "app.h"

namespace {

// 探测热键可用性时使用的临时 ID，避免与正式 ID 冲突
constexpr int kProbeHotkeyId = 0x7FFF;

}  // namespace

std::wstring RegisterAppHotkeys(HWND hwnd, const AppSettings& settings) {
    UnregisterAppHotkeys(hwnd);

    std::wstring failed;
    if (!RegisterHotKey(hwnd, kHotkeyIdRegion, settings.regionHotkey.modifiers | MOD_NOREPEAT,
                        settings.regionHotkey.vk)) {
        failed = HotkeyToString(settings.regionHotkey);
    } else if (!RegisterHotKey(hwnd, kHotkeyIdFullscreen, settings.fullscreenHotkey.modifiers | MOD_NOREPEAT,
                               settings.fullscreenHotkey.vk)) {
        failed = HotkeyToString(settings.fullscreenHotkey);
    }

    if (!failed.empty()) {
        // 保持"要么全部生效、要么全部不生效"，避免半注册状态
        UnregisterAppHotkeys(hwnd);
    }
    return failed;
}

void UnregisterAppHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, kHotkeyIdRegion);
    UnregisterHotKey(hwnd, kHotkeyIdFullscreen);
}

bool IsHotkeyAvailable(const HotkeyConfig& hotkey) {
    if (!hotkey.IsValid()) {
        return false;
    }
    if (RegisterHotKey(nullptr, kProbeHotkeyId, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk)) {
        UnregisterHotKey(nullptr, kProbeHotkeyId);
        return true;
    }
    // 若失败原因是本进程已注册同一组合，先注销本进程的注册再探测一次
    UnregisterAppHotkeys(nullptr);
    if (RegisterHotKey(nullptr, kProbeHotkeyId, hotkey.modifiers | MOD_NOREPEAT, hotkey.vk)) {
        UnregisterHotKey(nullptr, kProbeHotkeyId);
        return true;
    }
    return false;
}

