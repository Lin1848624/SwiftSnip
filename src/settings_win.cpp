// 瞬截 SwiftSnip - Windows 轻量截图工具
// Copyright (C) 2026 Lin1848624
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings_win.h"

#include "app.h"
#include "autostart.h"
#include "hotkey.h"
#include "settings.h"
#include "tray.h"

#include <commctrl.h>
#include <shlobj.h>

#include <string>

namespace {

// 控件 ID
constexpr int kIdHotkeyRegion = 3001;
constexpr int kIdHotkeyFullscreen = 3002;
constexpr int kIdSaveDirEdit = 3003;
constexpr int kIdBrowse = 3004;
constexpr int kIdScopeCombo = 3005;
constexpr int kIdAutoStart = 3006;
constexpr int kIdSave = 3007;
constexpr int kIdCancel = 3008;
constexpr int kIdOpenDir = 3009;
constexpr int kIdHotkeyScroll = 3010;

// 热键字段（区域 / 全屏 / 长截图）
enum class HotkeyField {
    Region = 0,
    Fullscreen = 1,
    Scroll = 2,
};

constexpr int kHotkeyFieldCount = 3;

struct SettingsWindowState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND regionButton = nullptr;
    HWND fullscreenButton = nullptr;
    HWND scrollButton = nullptr;
    HWND saveDirEdit = nullptr;
    HWND scopeCombo = nullptr;
    HWND autoStartCheck = nullptr;
    HotkeyConfig regionHotkey;
    HotkeyConfig fullscreenHotkey;
    HotkeyConfig scrollHotkey;
    bool recordingRegion = false;
    bool recordingFullscreen = false;
    bool recordingScroll = false;
    UINT dpi = 96;
    HFONT font = nullptr;
};

SettingsWindowState g_settings;

int Scale(int value) {
    return MulDiv(value, static_cast<int>(g_settings.dpi), 96);
}

std::wstring Trim(const std::wstring& text) {
    size_t begin = text.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos) {
        return L"";
    }
    size_t end = text.find_last_not_of(L" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

HotkeyField FieldFromSubclassId(UINT_PTR id) {
    if (id == kIdHotkeyFullscreen) {
        return HotkeyField::Fullscreen;
    }
    if (id == kIdHotkeyScroll) {
        return HotkeyField::Scroll;
    }
    return HotkeyField::Region;
}

HWND& ButtonFor(HotkeyField field) {
    if (field == HotkeyField::Fullscreen) {
        return g_settings.fullscreenButton;
    }
    if (field == HotkeyField::Scroll) {
        return g_settings.scrollButton;
    }
    return g_settings.regionButton;
}

HotkeyConfig& HotkeyFor(HotkeyField field) {
    if (field == HotkeyField::Fullscreen) {
        return g_settings.fullscreenHotkey;
    }
    if (field == HotkeyField::Scroll) {
        return g_settings.scrollHotkey;
    }
    return g_settings.regionHotkey;
}

bool& RecordingFor(HotkeyField field) {
    if (field == HotkeyField::Fullscreen) {
        return g_settings.recordingFullscreen;
    }
    if (field == HotkeyField::Scroll) {
        return g_settings.recordingScroll;
    }
    return g_settings.recordingRegion;
}

void UpdateHotkeyButtonText(HotkeyField field) {
    HWND button = ButtonFor(field);
    const bool recording = RecordingFor(field);
    const HotkeyConfig& hotkey = HotkeyFor(field);
    if (button == nullptr) {
        return;
    }
    SetWindowTextW(button, recording ? L"请按下组合键…" : HotkeyToString(hotkey).c_str());
}

void UpdateAllHotkeyButtons() {
    for (int index = 0; index < kHotkeyFieldCount; ++index) {
        UpdateHotkeyButtonText(static_cast<HotkeyField>(index));
    }
}

// 热键按钮子类过程：按下后进入录制状态，捕获下一个组合键
LRESULT CALLBACK HotkeyButtonProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR subclassId,
                                  DWORD_PTR) {
    const HotkeyField field = FieldFromSubclassId(subclassId);
    bool& recording = RecordingFor(field);
    HotkeyConfig& hotkey = HotkeyFor(field);

    switch (message) {
        case WM_LBUTTONDOWN:
            g_settings.recordingRegion = false;
            g_settings.recordingFullscreen = false;
            g_settings.recordingScroll = false;
            recording = true;
            SetFocus(hwnd);
            UpdateAllHotkeyButtons();
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (!recording) {
                break;
            }
            const UINT vk = static_cast<UINT>(wparam);
            if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN) {
                return 0;
            }
            if (vk == VK_ESCAPE) {
                recording = false;
                UpdateHotkeyButtonText(field);
                return 0;
            }

            HotkeyConfig candidate;
            candidate.modifiers = 0;
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                candidate.modifiers |= MOD_CONTROL;
            }
            if (GetKeyState(VK_MENU) & 0x8000) {
                candidate.modifiers |= MOD_ALT;
            }
            if (GetKeyState(VK_SHIFT) & 0x8000) {
                candidate.modifiers |= MOD_SHIFT;
            }
            if ((GetKeyState(VK_LWIN) & 0x8000) || (GetKeyState(VK_RWIN) & 0x8000)) {
                candidate.modifiers |= MOD_WIN;
            }
            candidate.vk = vk;

            if (candidate.modifiers == 0) {
                MessageBoxW(g_settings.hwnd, L"请至少包含 Ctrl、Alt、Shift、Win 中的一个修饰键。", L"热键无效",
                            MB_OK | MB_ICONWARNING);
                return 0;
            }

            hotkey = candidate;
            recording = false;
            UpdateHotkeyButtonText(field);
            return 0;
        }

        case WM_KILLFOCUS:
            if (recording) {
                recording = false;
                UpdateHotkeyButtonText(field);
            }
            break;

        default:
            break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void BrowseForFolder(HWND hwnd) {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    if (SUCCEEDED(dialog->Show(hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
                SetWindowTextW(g_settings.saveDirEdit, path);
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

void OnSaveClicked(HWND hwnd) {
    HotkeyConfig* hotkeys[kHotkeyFieldCount] = {&g_settings.regionHotkey, &g_settings.fullscreenHotkey,
                                                &g_settings.scrollHotkey};
    for (int i = 0; i < kHotkeyFieldCount; ++i) {
        for (int j = i + 1; j < kHotkeyFieldCount; ++j) {
            if (hotkeys[i]->vk == hotkeys[j]->vk && hotkeys[i]->modifiers == hotkeys[j]->modifiers) {
                MessageBoxW(hwnd, L"区域截图、长截图、全屏截图不能使用相同热键。", L"热键冲突",
                            MB_OK | MB_ICONWARNING);
                return;
            }
        }
    }

    wchar_t buffer[1024] = {};
    GetWindowTextW(g_settings.saveDirEdit, buffer, 1024);
    std::wstring saveDir = Trim(buffer);
    if (saveDir.empty()) {
        saveDir = Settings::Instance().DefaultSaveDir();
    }

    // 试注册新热键：先注销当前注册，探测完成后无论成败都恢复注册
    const AppSettings& current = Settings::Instance().Get();
    UnregisterAppHotkeys(g_settings.owner);
    bool allAvailable = true;
    for (int i = 0; i < kHotkeyFieldCount; ++i) {
        if (!IsHotkeyAvailable(*hotkeys[i])) {
            allAvailable = false;
            break;
        }
    }
    if (!allAvailable) {
        RegisterAppHotkeys(g_settings.owner, current);
        MessageBoxW(hwnd, L"所选热键已被其他程序占用，请更换后重试。", L"热键不可用", MB_OK | MB_ICONWARNING);
        return;
    }

    AppSettings& data = Settings::Instance().Mutable();
    data.regionHotkey = g_settings.regionHotkey;
    data.fullscreenHotkey = g_settings.fullscreenHotkey;
    data.scrollHotkey = g_settings.scrollHotkey;
    data.saveDir = (saveDir == Settings::Instance().DefaultSaveDir()) ? L"" : saveDir;

    const LRESULT scope = SendMessageW(g_settings.scopeCombo, CB_GETCURSEL, 0, 0);
    data.fullscreenScope = (scope == 1) ? FullscreenScope::AllMonitors : FullscreenScope::Primary;

    const bool autoStart = SendMessageW(g_settings.autoStartCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    data.autoStart = autoStart;
    Settings::Instance().Save();
    AutoStartSet(autoStart);

    const std::wstring failed = RegisterAppHotkeys(g_settings.owner, Settings::Instance().Get());
    if (!failed.empty()) {
        MessageBoxW(hwnd, (L"热键注册失败：" + failed).c_str(), L"错误", MB_OK | MB_ICONERROR);
    }

    TrayShowBalloon(L"设置已保存", L"新的设置已生效。");
    DestroyWindow(hwnd);
}

void LayoutControls() {
    const int labelWidth = Scale(96);
    const int controlX = Scale(120);
    const int controlWidth = Scale(190);
    const int rowHeight = Scale(26);
    const int rowGap = Scale(40);

    int y = Scale(18);
    HWND labelRegion = GetDlgItem(g_settings.hwnd, 3101);
    HWND labelFullscreen = GetDlgItem(g_settings.hwnd, 3102);
    HWND labelSaveDir = GetDlgItem(g_settings.hwnd, 3103);
    HWND labelScope = GetDlgItem(g_settings.hwnd, 3104);

    MoveWindow(labelRegion, Scale(18), y + Scale(4), labelWidth, rowHeight, TRUE);
    MoveWindow(g_settings.regionButton, controlX, y, controlWidth, rowHeight, TRUE);
    y += rowGap;

    MoveWindow(labelFullscreen, Scale(18), y + Scale(4), labelWidth, rowHeight, TRUE);
    MoveWindow(g_settings.fullscreenButton, controlX, y, controlWidth, rowHeight, TRUE);
    y += rowGap;

    MoveWindow(GetDlgItem(g_settings.hwnd, 3105), Scale(18), y + Scale(4), labelWidth, rowHeight, TRUE);
    MoveWindow(g_settings.scrollButton, controlX, y, controlWidth, rowHeight, TRUE);
    y += rowGap;

    MoveWindow(labelSaveDir, Scale(18), y + Scale(4), labelWidth, rowHeight, TRUE);
    MoveWindow(g_settings.saveDirEdit, controlX, y, controlWidth, rowHeight, TRUE);
    MoveWindow(GetDlgItem(g_settings.hwnd, kIdBrowse), controlX + controlWidth + Scale(8), y, Scale(80), rowHeight, TRUE);
    y += rowGap;

    MoveWindow(labelScope, Scale(18), y + Scale(4), labelWidth, rowHeight, TRUE);
    MoveWindow(g_settings.scopeCombo, controlX, y, controlWidth, Scale(200), TRUE);
    y += rowGap;

    MoveWindow(g_settings.autoStartCheck, controlX, y, controlWidth, rowHeight, TRUE);
    y += Scale(38);

    MoveWindow(GetDlgItem(g_settings.hwnd, kIdOpenDir), Scale(18), y, Scale(120), Scale(28), TRUE);
    MoveWindow(GetDlgItem(g_settings.hwnd, kIdSave), Scale(236), y, Scale(80), Scale(28), TRUE);
    MoveWindow(GetDlgItem(g_settings.hwnd, kIdCancel), Scale(324), y, Scale(80), Scale(28), TRUE);
}

void CreateControls(HWND hwnd) {
    HINSTANCE instance = GetModuleHandleW(nullptr);

    g_settings.font = CreateFontW(-MulDiv(9, static_cast<int>(g_settings.dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    auto create = [&](const wchar_t* className, const wchar_t* text, DWORD style, DWORD exStyle, int id) {
        HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (control != nullptr && g_settings.font != nullptr) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_settings.font), TRUE);
        }
        return control;
    };

    create(L"STATIC", L"区域截图热键：", SS_LEFT, 0, 3101);
    create(L"STATIC", L"全屏截图热键：", SS_LEFT, 0, 3102);
    create(L"STATIC", L"保存目录：", SS_LEFT, 0, 3103);
    create(L"STATIC", L"全屏范围：", SS_LEFT, 0, 3104);
    create(L"STATIC", L"长截图热键：", SS_LEFT, 0, 3105);

    g_settings.regionButton = create(L"BUTTON", L"", BS_PUSHBUTTON, 0, kIdHotkeyRegion);
    g_settings.fullscreenButton = create(L"BUTTON", L"", BS_PUSHBUTTON, 0, kIdHotkeyFullscreen);
    g_settings.scrollButton = create(L"BUTTON", L"", BS_PUSHBUTTON, 0, kIdHotkeyScroll);
    g_settings.saveDirEdit = create(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, kIdSaveDirEdit);
    create(L"BUTTON", L"浏览…", BS_PUSHBUTTON, 0, kIdBrowse);
    g_settings.scopeCombo = create(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 0, kIdScopeCombo);
    g_settings.autoStartCheck = create(L"BUTTON", L"开机自动启动", BS_AUTOCHECKBOX, 0, kIdAutoStart);
    create(L"BUTTON", L"打开保存目录", BS_PUSHBUTTON, 0, kIdOpenDir);
    create(L"BUTTON", L"保存", BS_DEFPUSHBUTTON, 0, kIdSave);
    create(L"BUTTON", L"取消", BS_PUSHBUTTON, 0, kIdCancel);

    SendMessageW(g_settings.scopeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"主显示器"));
    SendMessageW(g_settings.scopeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"所有显示器"));

    // 初始化各控件状态
    const AppSettings& data = Settings::Instance().Get();
    g_settings.regionHotkey = data.regionHotkey;
    g_settings.fullscreenHotkey = data.fullscreenHotkey;
    g_settings.scrollHotkey = data.scrollHotkey;
    UpdateAllHotkeyButtons();

    const std::wstring saveDir = Settings::Instance().EffectiveSaveDir();
    SetWindowTextW(g_settings.saveDirEdit, saveDir.c_str());
    SendMessageW(g_settings.scopeCombo, CB_SETCURSEL,
                 data.fullscreenScope == FullscreenScope::AllMonitors ? 1 : 0, 0);
    SendMessageW(g_settings.autoStartCheck, BM_SETCHECK, AutoStartIsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    SetWindowSubclass(g_settings.regionButton, HotkeyButtonProc, kIdHotkeyRegion, 0);
    SetWindowSubclass(g_settings.fullscreenButton, HotkeyButtonProc, kIdHotkeyFullscreen, 0);
    SetWindowSubclass(g_settings.scrollButton, HotkeyButtonProc, kIdHotkeyScroll, 0);

    LayoutControls();
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            g_settings.hwnd = hwnd;
            g_settings.dpi = GetDpiForWindow(hwnd);
            CreateControls(hwnd);
            return 0;

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            switch (id) {
                case kIdBrowse:
                    BrowseForFolder(hwnd);
                    return 0;
                case kIdOpenDir: {
                    const std::wstring dir = Settings::Instance().EffectiveSaveDir();
                    EnsureDirectoryExists(dir);
                    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                case kIdSave:
                    OnSaveClicked(hwnd);
                    return 0;
                case kIdCancel:
                    DestroyWindow(hwnd);
                    return 0;
                default:
                    break;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_settings.font != nullptr) {
                DeleteObject(g_settings.font);
                g_settings.font = nullptr;
            }
            g_settings.hwnd = nullptr;
            g_settings.regionButton = nullptr;
            g_settings.fullscreenButton = nullptr;
            g_settings.scrollButton = nullptr;
            g_settings.saveDirEdit = nullptr;
            g_settings.scopeCombo = nullptr;
            g_settings.autoStartCheck = nullptr;
            g_settings.recordingRegion = false;
            g_settings.recordingFullscreen = false;
            g_settings.recordingScroll = false;
            return 0;

        case WM_CTLCOLORSTATIC:
            SetBkMode(reinterpret_cast<HDC>(wparam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool EnsureSettingsClass() {
    static bool registered = false;
    if (registered) {
        return true;
    }
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kSettingsWndClass;
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResourceId));
    if (RegisterClassExW(&wc) == 0) {
        return false;
    }
    registered = true;
    return true;
}

}  // namespace

void ShowSettingsWindow(HWND owner) {
    if (g_settings.hwnd != nullptr && IsWindow(g_settings.hwnd)) {
        ShowWindow(g_settings.hwnd, SW_RESTORE);
        SetForegroundWindow(g_settings.hwnd);
        return;
    }
    if (!EnsureSettingsClass()) {
        return;
    }

    g_settings.owner = owner;
    g_settings.recordingRegion = false;
    g_settings.recordingFullscreen = false;
    g_settings.dpi = GetDpiForSystem();

    RECT rect = {0, 0, Scale(440), Scale(340)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int x = workArea.left + ((workArea.right - workArea.left) - width) / 2;
    const int y = workArea.top + ((workArea.bottom - workArea.top) - height) / 2;

    HWND hwnd = CreateWindowExW(0, kSettingsWndClass, L"瞬截 · 设置", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                                                                  WS_MINIMIZEBOX,
                                x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (hwnd == nullptr) {
        return;
    }
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
}

bool IsSettingsWindowOpen() {
    return g_settings.hwnd != nullptr && IsWindow(g_settings.hwnd) != FALSE;
}

void CloseSettingsWindow() {
    if (IsSettingsWindowOpen()) {
        DestroyWindow(g_settings.hwnd);
    }
}
