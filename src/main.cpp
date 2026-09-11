#include <windows.h>

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>

#include "app.h"
#include "capture.h"
#include "dpi.h"
#include "hotkey.h"
#include "overlay.h"
#include "png_writer.h"
#include "settings.h"
#include "settings_win.h"
#include "tray.h"

namespace {

HWND g_mainWnd = nullptr;

// 以 UTF-8（带 BOM）写入文本文件，供自检输出日志
bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return false;
    }

    // 先按含结尾 NUL 的长度分配，转换后去掉 NUL，避免日志文件尾部出现空字节
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    utf8.resize(static_cast<size_t>(size - 1));
    const std::string content = "\xEF\xBB\xBF" + utf8;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok =
        WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) != FALSE;
    CloseHandle(file);
    return ok;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t position = path.find_last_of(L"\\/");
    return position == std::wstring::npos ? path : path.substr(position + 1);
}

// 生成不重名的输出路径：SwiftSnip_yyyyMMdd_HHmmss.png
std::wstring MakeOutputPath(const std::wstring& dir) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    wchar_t name[128] = {};
    swprintf_s(name, L"SwiftSnip_%04d%02d%02d_%02d%02d%02d", time.wYear, time.wMonth, time.wDay, time.wHour,
               time.wMinute, time.wSecond);

    const std::wstring prefix = dir + L"\\" + name;
    std::wstring path = prefix + L".png";
    int index = 2;
    while (PathFileExistsW(path.c_str())) {
        path = prefix + L"_" + std::to_wstring(index) + L".png";
        ++index;
    }
    return path;
}

// 保存截图并给出气泡提示；无论成败都释放位图
void SaveCapturedImage(CapturedImage& image) {
    if (!image.Valid()) {
        return;
    }

    const std::wstring dir = Settings::Instance().EffectiveSaveDir();
    if (!EnsureDirectoryExists(dir)) {
        TrayShowBalloon(L"保存失败", L"无法创建保存目录：" + dir);
        image.Release();
        return;
    }

    const std::wstring path = MakeOutputPath(dir);
    std::wstring error;
    if (SaveBitmapAsPng(image.bitmap, path, &error)) {
        TrayShowBalloon(L"截图已保存", FileNameOf(path) + L"（" + std::to_wstring(image.width) + L" × " +
                                           std::to_wstring(image.height) + L"）");
    } else {
        TrayShowBalloon(L"保存失败", error);
    }
    image.Release();

    // 截图位图已释放，修剪工作集，保持常驻内存处于低位
    SetProcessWorkingSetSizeEx(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1), 0);
}

void CaptureFullscreenAndSave() {
    if (IsRegionCaptureActive()) {
        return;
    }

    CapturedImage image;
    const bool ok = Settings::Instance().Get().fullscreenScope == FullscreenScope::AllMonitors
                        ? CaptureVirtualScreen(&image)
                        : CapturePrimaryMonitor(&image);
    if (!ok) {
        TrayShowBalloon(L"截图失败", L"无法捕获屏幕内容。");
        return;
    }
    SaveCapturedImage(image);
}

void CaptureRegionAndSave() {
    if (IsRegionCaptureActive()) {
        return;
    }
    if (IsSettingsWindowOpen()) {
        CloseSettingsWindow();
    }

    CapturedImage fullScreen;
    if (!CaptureVirtualScreen(&fullScreen)) {
        TrayShowBalloon(L"截图失败", L"无法捕获屏幕内容。");
        return;
    }

    const bool started = StartRegionCapture(g_mainWnd, fullScreen, [](CapturedImage result) {
        if (result.Valid()) {
            SaveCapturedImage(result);
        }
    });
    if (!started) {
        // 启动失败时所有权仍在本函数，需要自行释放
        fullScreen.Release();
        TrayShowBalloon(L"截图失败", L"无法启动区域选择。");
    }
}

void OpenSaveDirectory() {
    const std::wstring dir = Settings::Instance().EffectiveSaveDir();
    EnsureDirectoryExists(dir);
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ShowAbout() {
    const std::wstring text = std::wstring(L"瞬截 SwiftSnip ") + kAppVersion +
                              L"\n\n轻量截图工具：区域截图与全屏截图，自动保存 PNG。\n\n"
                              L"区域截图：拖动鼠标选择区域，Enter 确认，Esc 或右键取消。";
    MessageBoxW(g_mainWnd, text.c_str(), L"关于瞬截", MB_OK | MB_ICONINFORMATION);
}

void HandleTrayMenu() {
    const UINT command = TrayShowContextMenu(g_mainWnd);
    switch (command) {
        case kCmdCaptureRegion:
            CaptureRegionAndSave();
            break;
        case kCmdCaptureFullscreen:
            CaptureFullscreenAndSave();
            break;
        case kCmdOpenSaveDir:
            OpenSaveDirectory();
            break;
        case kCmdOpenSettings:
            ShowSettingsWindow(g_mainWnd);
            break;
        case kCmdAbout:
            ShowAbout();
            break;
        case kCmdExit:
            DestroyWindow(g_mainWnd);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_HOTKEY:
            if (wparam == kHotkeyIdRegion) {
                CaptureRegionAndSave();
            } else if (wparam == kHotkeyIdFullscreen) {
                CaptureFullscreenAndSave();
            }
            return 0;

        case WM_APP_TRAY: {
            const UINT event = LOWORD(lparam);
            if (event == WM_LBUTTONUP) {
                CaptureRegionAndSave();
            } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                HandleTrayMenu();
            }
            return 0;
        }

        case WM_APP_OPEN_SETTINGS:
            ShowSettingsWindow(hwnd);
            return 0;

        case WM_DESTROY:
            CancelRegionCapture();
            CloseSettingsWindow();
            UnregisterAppHotkeys(hwnd);
            TrayShutdown();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

// 自检：捕获主显示器并保存 PNG，结果写入 <输出路径>.log
int RunSelfTest(const std::wstring& outputPath) {
    std::wstring log;
    CapturedImage image;
    if (!CapturePrimaryMonitor(&image)) {
        log = L"capture: FAIL";
    } else {
        log = L"capture: OK " + std::to_wstring(image.width) + L"x" + std::to_wstring(image.height);
        std::wstring error;
        if (SaveBitmapAsPng(image.bitmap, outputPath, &error)) {
            log += L"\nsave: OK " + outputPath;
            log += L"\norigin: " + std::to_wstring(image.originX) + L"," + std::to_wstring(image.originY);
        } else {
            log += L"\nsave: FAIL " + error;
        }
        image.Release();
    }

    const bool ok = log.find(L"FAIL") == std::wstring::npos;
    WriteUtf8File(outputPath + L".log", log + L"\nresult: " + (ok ? L"PASS" : L"FAIL") + L"\n");
    return ok ? 0 : 2;
}

// 检查默认热键是否可用，结果写入日志
int RunCheckHotkeys(const std::wstring& logPath) {
    Settings::Instance().Load();
    const AppSettings& settings = Settings::Instance().Get();
    const bool region = IsHotkeyAvailable(settings.regionHotkey);
    const bool fullscreen = IsHotkeyAvailable(settings.fullscreenHotkey);

    std::wstring log = L"region(" + HotkeyToString(settings.regionHotkey) + L")=" + (region ? L"OK" : L"BUSY") +
                       L"\nfullscreen(" + HotkeyToString(settings.fullscreenHotkey) + L")=" +
                       (fullscreen ? L"OK" : L"BUSY") + L"\n";
    WriteUtf8File(logPath, log);
    return (region && fullscreen) ? 0 : 1;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    InitDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX commonControls = {};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&commonControls);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    int exitCode = 0;
    bool commandHandled = false;

    if (argv != nullptr && argc >= 3) {
        if (_wcsicmp(argv[1], L"--selftest") == 0) {
            exitCode = RunSelfTest(argv[2]);
            commandHandled = true;
        } else if (_wcsicmp(argv[1], L"--check-hotkeys") == 0) {
            exitCode = RunCheckHotkeys(argv[2]);
            commandHandled = true;
        }
    }

    if (!commandHandled) {
        // 单实例：已存在实例时唤起它的设置窗口
        HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
        if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existing = FindWindowW(kMainWndClass, nullptr);
            if (existing != nullptr) {
                PostMessageW(existing, WM_APP_OPEN_SETTINGS, 0, 0);
            }
            CloseHandle(mutex);
            LocalFree(argv);
            CoUninitialize();
            return 0;
        }

        Settings::Instance().Load();

        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = MainWndProc;
        wc.hInstance = instance;
        wc.lpszClassName = kMainWndClass;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kIconResourceId));
        RegisterClassExW(&wc);

        g_mainWnd = CreateWindowExW(0, kMainWndClass, kAppName, WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                    instance, nullptr);
        if (g_mainWnd == nullptr) {
            if (mutex != nullptr) {
                CloseHandle(mutex);
            }
            LocalFree(argv);
            CoUninitialize();
            return 1;
        }

        if (!TrayInitialize(g_mainWnd)) {
            MessageBoxW(nullptr, L"托盘图标创建失败，程序即将退出。", kAppName, MB_OK | MB_ICONERROR);
        }

        const std::wstring failed = RegisterAppHotkeys(g_mainWnd, Settings::Instance().Get());
        if (!failed.empty()) {
            TrayShowBalloon(L"热键注册失败", failed + L" 已被其他程序占用，请在设置中修改。");
        }

        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        exitCode = static_cast<int>(message.wParam);

        if (mutex != nullptr) {
            CloseHandle(mutex);
        }
    }

    if (argv != nullptr) {
        LocalFree(argv);
    }
    CoUninitialize();
    return exitCode;
}
