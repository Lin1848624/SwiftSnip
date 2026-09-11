"""测量从触发热键到区域选择遮罩窗口出现的延迟。"""

import ctypes
import subprocess
import time
from ctypes import wintypes

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

WM_HOTKEY = 0x0312
WM_KEYDOWN = 0x0100
WM_CLOSE = 0x0010
VK_ESCAPE = 0x1B

process = subprocess.Popen([EXE])
try:
    deadline = time.time() + 10
    main = 0
    while not main and time.time() < deadline:
        main = user32.FindWindowW("SwiftSnipMainWnd", None)
        time.sleep(0.005)
    if not main:
        raise RuntimeError("主窗口未出现")

    samples = []
    for _ in range(5):
        start = time.perf_counter()
        user32.PostMessageW(main, WM_HOTKEY, 1, 0)
        overlay = 0
        while not overlay:
            overlay = user32.FindWindowW("SwiftSnipOverlayWnd", None)
            if time.perf_counter() - start > 5:
                raise RuntimeError("遮罩未出现")
            time.sleep(0.001)
        samples.append((time.perf_counter() - start) * 1000)
        user32.PostMessageW(overlay, WM_KEYDOWN, VK_ESCAPE, 0)
        time.sleep(0.6)

    print("overlay latency samples (ms):", [round(value, 1) for value in samples])
    print(f"min={min(samples):.1f} ms  avg={sum(samples) / len(samples):.1f} ms")
finally:
    window = user32.FindWindowW("SwiftSnipMainWnd", None)
    if window:
        user32.PostMessageW(window, WM_CLOSE, 0, 0)
    try:
        process.wait(timeout=6)
    except subprocess.TimeoutExpired:
        process.kill()
