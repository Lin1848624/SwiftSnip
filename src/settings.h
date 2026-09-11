#pragma once

#include <windows.h>

#include <string>

// 热键配置：修饰符（MOD_*）+ 虚拟键码
struct HotkeyConfig {
    UINT modifiers = MOD_CONTROL | MOD_ALT;
    UINT vk = 'A';

    bool IsValid() const { return vk != 0 && modifiers != 0; }
};

// 全屏截图范围
enum class FullscreenScope : int {
    Primary = 0,      // 仅主显示器
    AllMonitors = 1,  // 所有显示器
};

struct AppSettings {
    HotkeyConfig regionHotkey{MOD_CONTROL | MOD_ALT, 'A'};
    HotkeyConfig fullscreenHotkey{MOD_CONTROL | MOD_ALT, 'F'};
    std::wstring saveDir;  // 为空时使用默认目录
    FullscreenScope fullscreenScope = FullscreenScope::Primary;
    bool autoStart = false;
};

// 热键与文本互转，例如 "Ctrl+Alt+A"
std::wstring HotkeyToString(const HotkeyConfig& hotkey);
bool HotkeyFromString(const std::wstring& text, HotkeyConfig* out);

// 递归创建目录；已存在视为成功
bool EnsureDirectoryExists(const std::wstring& dir);

class Settings {
public:
    static Settings& Instance();

    void Load();
    void Save() const;

    const AppSettings& Get() const { return m_data; }
    AppSettings& Mutable() { return m_data; }

    std::wstring ExePath() const;           // 当前 exe 完整路径
    std::wstring ExeDir() const;            // exe 所在目录，无尾随反斜杠
    std::wstring DefaultSaveDir() const;    // <exe目录>\Pictures
    std::wstring EffectiveSaveDir() const;  // saveDir 为空时返回默认目录
    std::wstring ConfigPath() const;        // %APPDATA%\SwiftSnip\config.ini

private:
    Settings() = default;

    AppSettings m_data;
};

