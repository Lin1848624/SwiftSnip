#include "settings.h"

#include "app.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <cwchar>

namespace {

// 返回已知文件夹路径；失败时返回空串
std::wstring GetKnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)) || raw == nullptr) {
        return L"";
    }
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

struct KeyName {
    UINT vk;
    const wchar_t* name;
};

const KeyName kKeyNames[] = {
    {VK_SNAPSHOT, L"PrintScreen"}, {VK_INSERT, L"Insert"},   {VK_DELETE, L"Delete"},
    {VK_HOME, L"Home"},           {VK_END, L"End"},          {VK_PRIOR, L"PageUp"},
    {VK_NEXT, L"PageDown"},       {VK_UP, L"Up"},            {VK_DOWN, L"Down"},
    {VK_LEFT, L"Left"},           {VK_RIGHT, L"Right"},      {VK_SPACE, L"Space"},
    {VK_TAB, L"Tab"},             {VK_RETURN, L"Enter"},     {VK_OEM_3, L"`"},
    {VK_OEM_MINUS, L"-"},         {VK_OEM_PLUS, L"="},       {VK_OEM_4, L"["},
    {VK_OEM_6, L"]"},             {VK_OEM_5, L"\\"},         {VK_OEM_1, L";"},
    {VK_OEM_7, L"'"},             {VK_OEM_COMMA, L","},      {VK_OEM_PERIOD, L"."},
    {VK_OEM_2, L"/"},
};

bool EqualsIgnoreCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

// 虚拟键码转显示名
bool VkToName(UINT vk, std::wstring* out) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        out->assign(1, static_cast<wchar_t>(vk));
        return true;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        *out = L"F" + std::to_wstring(vk - VK_F1 + 1);
        return true;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        *out = L"Num" + std::to_wstring(vk - VK_NUMPAD0);
        return true;
    }
    for (const KeyName& item : kKeyNames) {
        if (item.vk == vk) {
            *out = item.name;
            return true;
        }
    }
    return false;
}

// 显示名转虚拟键码
bool NameToVk(const std::wstring& name, UINT* out) {
    if (name.empty()) {
        return false;
    }
    if (name.size() == 1) {
        wchar_t ch = name[0];
        if (ch >= L'a' && ch <= L'z') {
            ch = static_cast<wchar_t>(ch - L'a' + L'A');
        }
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9')) {
            *out = static_cast<UINT>(ch);
            return true;
        }
    }
    if ((name[0] == L'F' || name[0] == L'f') && name.size() >= 2 && name.size() <= 3) {
        int number = _wtoi(name.c_str() + 1);
        if (number >= 1 && number <= 24) {
            *out = static_cast<UINT>(VK_F1 + number - 1);
            return true;
        }
    }
    if ((name[0] == L'N' || name[0] == L'n') && name.size() == 4 && _wcsnicmp(name.c_str(), L"Num", 3) == 0) {
        wchar_t digit = name[3];
        if (digit >= L'0' && digit <= L'9') {
            *out = static_cast<UINT>(VK_NUMPAD0 + (digit - L'0'));
            return true;
        }
    }
    for (const KeyName& item : kKeyNames) {
        if (EqualsIgnoreCase(name, item.name)) {
            *out = item.vk;
            return true;
        }
    }
    // 支持 "0x41" 形式的十六进制回退
    if (name.size() > 2 && name[0] == L'0' && (name[1] == L'x' || name[1] == L'X')) {
        wchar_t* end = nullptr;
        long value = wcstol(name.c_str() + 2, &end, 16);
        if (end != nullptr && *end == L'\0' && value > 0 && value <= 0xFF) {
            *out = static_cast<UINT>(value);
            return true;
        }
    }
    return false;
}

}  // namespace

std::wstring HotkeyToString(const HotkeyConfig& hotkey) {
    std::wstring text;
    if (hotkey.modifiers & MOD_CONTROL) {
        text += L"Ctrl+";
    }
    if (hotkey.modifiers & MOD_ALT) {
        text += L"Alt+";
    }
    if (hotkey.modifiers & MOD_SHIFT) {
        text += L"Shift+";
    }
    if (hotkey.modifiers & MOD_WIN) {
        text += L"Win+";
    }
    std::wstring key;
    if (VkToName(hotkey.vk, &key)) {
        text += key;
    } else {
        wchar_t buffer[16] = {};
        swprintf_s(buffer, L"0x%02X", hotkey.vk);
        text += buffer;
    }
    return text;
}

bool HotkeyFromString(const std::wstring& text, HotkeyConfig* out) {
    if (out == nullptr || text.empty()) {
        return false;
    }
    HotkeyConfig result;
    result.modifiers = 0;
    result.vk = 0;

    size_t start = 0;
    bool hasKey = false;
    while (start <= text.size()) {
        size_t pos = text.find(L'+', start);
        std::wstring token = (pos == std::wstring::npos) ? text.substr(start) : text.substr(start, pos - start);
        // 去除首尾空格
        size_t begin = token.find_first_not_of(L" \t");
        size_t end = token.find_last_not_of(L" \t");
        token = (begin == std::wstring::npos) ? L"" : token.substr(begin, end - begin + 1);

        if (!token.empty()) {
            if (EqualsIgnoreCase(token, L"Ctrl") || EqualsIgnoreCase(token, L"Control")) {
                result.modifiers |= MOD_CONTROL;
            } else if (EqualsIgnoreCase(token, L"Alt")) {
                result.modifiers |= MOD_ALT;
            } else if (EqualsIgnoreCase(token, L"Shift")) {
                result.modifiers |= MOD_SHIFT;
            } else if (EqualsIgnoreCase(token, L"Win") || EqualsIgnoreCase(token, L"Windows")) {
                result.modifiers |= MOD_WIN;
            } else {
                UINT vk = 0;
                if (!NameToVk(token, &vk)) {
                    return false;
                }
                result.vk = vk;
                hasKey = true;
            }
        }
        if (pos == std::wstring::npos) {
            break;
        }
        start = pos + 1;
    }

    if (!hasKey || result.vk == 0) {
        return false;
    }
    *out = result;
    return true;
}

bool EnsureDirectoryExists(const std::wstring& dir) {
    if (dir.empty()) {
        return false;
    }
    int result = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

Settings& Settings::Instance() {
    static Settings instance;
    return instance;
}

void Settings::Load() {
    m_data = AppSettings{};

    const std::wstring path = ConfigPath();
    wchar_t buffer[512] = {};

    GetPrivateProfileStringW(L"Hotkeys", L"Region", L"Ctrl+Alt+A", buffer, 512, path.c_str());
    HotkeyConfig parsed;
    if (HotkeyFromString(buffer, &parsed)) {
        m_data.regionHotkey = parsed;
    }

    GetPrivateProfileStringW(L"Hotkeys", L"Fullscreen", L"Ctrl+Alt+F", buffer, 512, path.c_str());
    if (HotkeyFromString(buffer, &parsed)) {
        m_data.fullscreenHotkey = parsed;
    }

    GetPrivateProfileStringW(L"General", L"SaveDir", L"", buffer, 512, path.c_str());
    m_data.saveDir = buffer;

    m_data.fullscreenScope =
        GetPrivateProfileIntW(L"General", L"FullscreenScope", 0, path.c_str()) == 1
            ? FullscreenScope::AllMonitors
            : FullscreenScope::Primary;
    m_data.autoStart = GetPrivateProfileIntW(L"General", L"AutoStart", 0, path.c_str()) != 0;
}

void Settings::Save() const {
    const std::wstring path = ConfigPath();
    std::wstring dir = path;
    size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        dir.resize(slash);
        EnsureDirectoryExists(dir);
    }

    WritePrivateProfileStringW(L"Hotkeys", L"Region", HotkeyToString(m_data.regionHotkey).c_str(), path.c_str());
    WritePrivateProfileStringW(L"Hotkeys", L"Fullscreen", HotkeyToString(m_data.fullscreenHotkey).c_str(), path.c_str());
    WritePrivateProfileStringW(L"General", L"SaveDir", m_data.saveDir.c_str(), path.c_str());
    WritePrivateProfileStringW(L"General", L"FullscreenScope",
                               m_data.fullscreenScope == FullscreenScope::AllMonitors ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"General", L"AutoStart", m_data.autoStart ? L"1" : L"0", path.c_str());
}

std::wstring Settings::ExePath() const {
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }
    return buffer;
}

std::wstring Settings::ExeDir() const {
    std::wstring path = ExePath();
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }
    return path.substr(0, slash);
}

std::wstring Settings::DefaultSaveDir() const {
    std::wstring dir = ExeDir();
    if (dir.empty()) {
        return L"";
    }
    return dir + L"\\Pictures";
}

std::wstring Settings::EffectiveSaveDir() const {
    return m_data.saveDir.empty() ? DefaultSaveDir() : m_data.saveDir;
}

std::wstring Settings::ConfigPath() const {
    std::wstring appData = GetKnownFolder(FOLDERID_RoamingAppData);
    if (appData.empty()) {
        return L"";
    }
    return appData + L"\\" + kAppId + L"\\config.ini";
}
