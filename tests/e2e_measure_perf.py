"""测量 SwiftSnip 冷启动耗时与常驻内存。"""

import ctypes
import subprocess
import time
from ctypes import wintypes

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]

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


def one_run():
    start = time.perf_counter()
    process = subprocess.Popen([EXE])
    while True:
        window = user32.FindWindowW("SwiftSnipMainWnd", None)
        if window:
            break
        if time.perf_counter() - start > 10:
            raise RuntimeError("启动超时")
        time.sleep(0.002)
    startup_ms = (time.perf_counter() - start) * 1000

    time.sleep(1.5)
    handle = kernel32.OpenProcess(0x1000, False, process.pid)
    counters = PROCESS_MEMORY_COUNTERS()
    counters.cb = ctypes.sizeof(counters)
    psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
    kernel32.CloseHandle(handle)

    user32.PostMessageW(user32.FindWindowW("SwiftSnipMainWnd", None), 0x0010, 0, 0)
    process.wait(timeout=6)

    return startup_ms, counters.WorkingSetSize / 1048576, counters.PeakWorkingSetSize / 1048576


results = []
for _ in range(3):
    results.append(one_run())
    time.sleep(0.5)

for index, (startup, working, peak) in enumerate(results, 1):
    print(f"run{index}: startup={startup:.1f} ms  working_set={working:.2f} MB  peak={peak:.2f} MB")

best = min(results, key=lambda item: item[0])
print(f"best startup={best[0]:.1f} ms  min working_set={min(r[1] for r in results):.2f} MB")
