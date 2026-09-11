"""连续 100 次打开/取消区域选择遮罩，检查 GDI 对象与内存是否泄漏。"""

import ctypes
import subprocess
import time
from ctypes import wintypes

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"
ROUNDS = 300
REPORT_EVERY = 50

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.GetGuiResources.argtypes = [wintypes.HANDLE, wintypes.DWORD]
user32.GetGuiResources.restype = wintypes.DWORD

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]


def snapshot(process_handle):
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(counters)
    psapi.GetProcessMemoryInfo(process_handle, ctypes.byref(counters), counters.cb)
    gdi = user32.GetGuiResources(process_handle, 0)
    user = user32.GetGuiResources(process_handle, 1)
    return counters.WorkingSetSize / 1048576, gdi, user


process = subprocess.Popen([EXE])
try:
    deadline = time.time() + 10
    while not user32.FindWindowW("SwiftSnipMainWnd", None):
        if time.time() > deadline:
            raise RuntimeError("主窗口未出现")
        time.sleep(0.005)
    main = user32.FindWindowW("SwiftSnipMainWnd", None)
    handle = kernel32.OpenProcess(0x1000, False, process.pid)

    time.sleep(1.0)
    before = snapshot(handle)

    samples = []
    for index in range(ROUNDS):
        user32.PostMessageW(main, 0x0312, 1, 0)  # WM_HOTKEY 区域截图
        overlay = 0
        start = time.time()
        while not overlay and time.time() - start < 5:
            overlay = user32.FindWindowW("SwiftSnipOverlayWnd", None)
            time.sleep(0.001)
        if not overlay:
            raise RuntimeError(f"第 {index + 1} 次遮罩未出现")
        user32.PostMessageW(overlay, 0x0100, 0x1B, 0)  # WM_KEYDOWN Esc 取消
        # 等待窗口销毁，避免下一次 FindWindow 误判
        start = time.time()
        while user32.FindWindowW("SwiftSnipOverlayWnd", None) and time.time() - start < 5:
            time.sleep(0.001)
        if (index + 1) % REPORT_EVERY == 0:
            sample = snapshot(handle)
            samples.append((index + 1,) + sample)
            print(f"rounds={index + 1:4d}  working={sample[0]:7.2f} MB  gdi={sample[1]:3d}  user={sample[2]:3d}")

    time.sleep(1.5)
    after = snapshot(handle)

    print(f"rounds: {ROUNDS}")
    print(f"working set before: {before[0]:.2f} MB -> after: {after[0]:.2f} MB "
          f"(delta {after[0] - before[0]:+.2f} MB)")
    print(f"GDI objects before: {before[1]} -> after: {after[1]} (delta {after[1] - before[1]:+d})")
    print(f"USER objects before: {before[2]} -> after: {after[2]} (delta {after[2] - before[2]:+d})")

    # 首次创建遮罩会有一次性缓存，因此以第 50 轮为基线，检查后续是否持续增长
    baseline = samples[0]
    final = samples[-1]
    gdi_delta = final[2] - baseline[2]
    user_delta = final[3] - baseline[3]
    memory_delta = final[1] - baseline[1]
    print(f"stability from round {baseline[0]} to {final[0]}: "
          f"gdi={gdi_delta:+d} user={user_delta:+d} working={memory_delta:+.2f} MB")
    verdict = abs(gdi_delta) <= 5 and abs(user_delta) <= 5 and memory_delta < 5
    print("PASS 无对象/内存泄漏（长稳）" if verdict else "FAIL 存在泄漏")
finally:
    window = user32.FindWindowW("SwiftSnipMainWnd", None)
    if window:
        user32.PostMessageW(window, 0x0010, 0, 0)
    try:
        process.wait(timeout=6)
    except subprocess.TimeoutExpired:
        process.kill()
