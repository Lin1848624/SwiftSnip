// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#include "autostart.h"

#include "app.h"
#include "settings.h"

#include <string>

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"SwiftSnip";

}  // namespace

bool AutoStartIsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }
    // 仅当注册的路径与当前 exe 一致时才算开启
    return std::wstring(buffer) == L"\"" + Settings::Instance().ExePath() + L"\"";
}

bool AutoStartSet(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }

    bool ok = false;
    if (enabled) {
        const std::wstring command = L"\"" + Settings::Instance().ExePath() + L"\"";
        ok = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        LONG result = RegDeleteValueW(key, kValueName);
        ok = result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
    }

    RegCloseKey(key);
    return ok;
}
