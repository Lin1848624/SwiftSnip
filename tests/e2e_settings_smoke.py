"""设置窗口冒烟测试：

1. 启动主实例
2. 启动第二个实例，验证单实例逻辑并唤起设置窗口
3. 校验设置窗口与控件存在
4. 关闭设置窗口与主程序
"""

import ctypes
import subprocess
import time
from ctypes import wintypes

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
user32.GetDlgItem.restype = wintypes.HWND
user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
user32.IsWindowVisible.argtypes = [wintypes.HWND]


def wait_window(class_name, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        window = user32.FindWindowW(class_name, None)
        if window:
            return window
        time.sleep(0.02)
    return 0


main_process = subprocess.Popen([EXE])
second_process = None
try:
    main = wait_window("SwiftSnipMainWnd")
    if not main:
        raise RuntimeError("主窗口未出现")

    # 第二个实例应立即退出并把设置窗口带到前台
    second_process = subprocess.Popen([EXE])
    settings = wait_window("SwiftSnipSettingsWnd")
    if not settings:
        raise RuntimeError("第二个实例未唤起设置窗口")

    second_exit = second_process.wait(timeout=8)
    print(f"second instance exit code: {second_exit}")
    if second_exit != 0:
        print("FAIL 第二个实例未正常退出")
        raise SystemExit(1)

    controls = {
        "hotkey region button": 3001,
        "hotkey fullscreen button": 3002,
        "hotkey scroll button": 3010,
        "save dir edit": 3003,
        "browse button": 3004,
        "scope combo": 3005,
        "autostart check": 3006,
        "save button": 3007,
        "cancel button": 3008,
        "open dir button": 3009,
    }
    missing = []
    for name, control_id in controls.items():
        if not user32.GetDlgItem(settings, control_id):
            missing.append(name)
    if missing:
        print(f"FAIL 缺少控件：{missing}")
        raise SystemExit(1)
    print(f"settings window: 0x{settings:X}, visible: {bool(user32.IsWindowVisible(settings))}")
    print(f"controls found: {len(controls)}/{len(controls)}")
    print("PASS 单实例唤醒与设置窗口创建正常")

    user32.PostMessageW(settings, 0x0010, 0, 0)  # WM_CLOSE
    time.sleep(0.5)
finally:
    window = user32.FindWindowW("SwiftSnipMainWnd", None)
    if window:
        user32.PostMessageW(window, 0x0010, 0, 0)
    try:
        main_process.wait(timeout=6)
    except subprocess.TimeoutExpired:
        main_process.kill()
    if second_process is not None and second_process.poll() is None:
        second_process.kill()
