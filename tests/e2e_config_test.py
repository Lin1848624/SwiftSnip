"""配置持久化与自定义保存目录测验证：

写入自定义配置（热键 Ctrl+Shift+Z、保存目录 CustomPics、全屏范围=所有显示器），
启动程序触发全屏截图，校验文件落到自定义目录且尺寸正确，最后恢复默认配置。
"""

import ctypes
import glob
import os
import subprocess
import time
from ctypes import wintypes

from PIL import Image

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"
CONFIG = os.path.join(os.environ["APPDATA"], "SwiftSnip", "config.ini")
CUSTOM_DIR = r"D:\project\SwiftSnip\build\CustomPics"

DEFAULT_CONFIG = """[Hotkeys]
Region=Ctrl+Alt+A
Fullscreen=Ctrl+Alt+F
[General]
SaveDir=
FullscreenScope=0
AutoStart=0
"""

TEST_CONFIG = f"""[Hotkeys]
Region=Ctrl+Shift+Z
Fullscreen=Ctrl+Alt+F
[General]
SaveDir={CUSTOM_DIR}
FullscreenScope=1
AutoStart=0
"""

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def write_config(text):
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    with open(CONFIG, "w", encoding="utf-16") as handle:
        handle.write(text)


def wait_window(class_name, timeout=8.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        window = user32.FindWindowW(class_name, None)
        if window:
            return window
        time.sleep(0.02)
    return 0


exit_code = 1
write_config(TEST_CONFIG)
before = set(glob.glob(os.path.join(CUSTOM_DIR, "*.png")))

process = subprocess.Popen([EXE])
try:
    main = wait_window("SwiftSnipMainWnd")
    if not main:
        raise RuntimeError("主窗口未出现")

    # 全屏截图（热键 ID = 2）
    user32.PostMessageW(main, 0x0312, 2, 0)
    deadline = time.time() + 8
    new_files = []
    while time.time() < deadline:
        new_files = sorted(set(glob.glob(os.path.join(CUSTOM_DIR, "*.png"))) - before)
        if new_files:
            break
        time.sleep(0.1)

    if not new_files:
        print("FAIL 自定义目录中未生成文件")
        exit_code = 1
    else:
        path = new_files[0]
        with Image.open(path) as image:
            size = image.size
        print(f"captured: {path}")
        print(f"size: {size[0]}x{size[1]}")
        if size == (1920, 1080):
            print("PASS 自定义保存目录与全屏截图正常")
            exit_code = 0
        else:
            print("FAIL 全屏截图尺寸不符")
finally:
    window = user32.FindWindowW("SwiftSnipMainWnd", None)
    if window:
        user32.PostMessageW(window, 0x0010, 0, 0)
    try:
        process.wait(timeout=6)
    except subprocess.TimeoutExpired:
        process.kill()
    write_config(DEFAULT_CONFIG)
    print("配置已恢复为默认值")

raise SystemExit(exit_code)
