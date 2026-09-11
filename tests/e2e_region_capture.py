"""瞬截 / SwiftSnip 端到端集成测试。

启动程序 → 触发区域截图热键 → 向遮罩窗口发送拖拽与回车消息 → 校验输出 PNG。
不移动真实鼠标，仅发送窗口消息。
"""

import ctypes
import glob
import os
import subprocess
import time
from ctypes import wintypes

from PIL import Image

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"


def resolve_save_dir():
    """读取程序配置中的保存目录；未配置时回退到 exe 同级的 Pictures。"""
    config = os.path.join(os.environ.get("APPDATA", ""), "SwiftSnip", "config.ini")
    if os.path.exists(config):
        for encoding in ("utf-16", "utf-8-sig", "utf-8"):
            try:
                with open(config, encoding=encoding) as handle:
                    for line in handle:
                        stripped = line.strip()
                        if stripped.lower().startswith("savedir="):
                            value = stripped.split("=", 1)[1].strip()
                            if value:
                                return value
                break
            except (UnicodeError, OSError):
                continue
    return os.path.join(os.path.dirname(EXE), "Pictures")


PICTURES = resolve_save_dir()

MAIN_CLASS = "SwiftSnipMainWnd"
OVERLAY_CLASS = "SwiftSnipOverlayWnd"

WM_HOTKEY = 0x0312
WM_LBUTTONDOWN = 0x0201
WM_MOUSEMOVE = 0x0200
WM_LBUTTONUP = 0x0202
WM_KEYDOWN = 0x0100
WM_CLOSE = 0x0010
VK_RETURN = 0x0D
MK_LBUTTON = 0x0001

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.PostMessageW.restype = wintypes.BOOL


def make_lparam(x, y):
    return (y << 16) | (x & 0xFFFF)


def find_window(class_name):
    return user32.FindWindowW(class_name, None)


def main():
    listing = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq SwiftSnip.exe", "/NH"],
        capture_output=True,
        text=True,
    )
    if "SwiftSnip.exe" in listing.stdout:
        print("FAIL 已有 SwiftSnip 实例在运行，测试中止")
        return 2

    before = set(glob.glob(os.path.join(PICTURES, "*.png")))
    process = subprocess.Popen([EXE])
    failed = False
    try:
        time.sleep(1.2)
        if process.poll() is not None:
            print("FAIL 程序启动后立即退出")
            return 1

        main_wnd = find_window(MAIN_CLASS)
        if not main_wnd:
            print("FAIL 未找到主窗口")
            return 1
        print(f"main window: 0x{main_wnd:X}")

        # 触发区域截图（热键 ID = 1）
        user32.PostMessageW(main_wnd, WM_HOTKEY, 1, 0)
        time.sleep(0.8)

        overlay = find_window(OVERLAY_CLASS)
        if not overlay:
            print("FAIL 遮罩窗口未出现")
            return 1
        print(f"overlay window: 0x{overlay:X}")

        # 模拟拖拽：300,300 → 700,500，预期输出 400x200
        user32.PostMessageW(overlay, WM_LBUTTONDOWN, MK_LBUTTON, make_lparam(300, 300))
        time.sleep(0.15)
        user32.PostMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, make_lparam(500, 400))
        time.sleep(0.15)
        user32.PostMessageW(overlay, WM_MOUSEMOVE, MK_LBUTTON, make_lparam(700, 500))
        time.sleep(0.2)
        user32.PostMessageW(overlay, WM_LBUTTONUP, 0, make_lparam(700, 500))
        time.sleep(0.3)
        user32.PostMessageW(overlay, WM_KEYDOWN, VK_RETURN, 0)
        time.sleep(1.2)

        after = set(glob.glob(os.path.join(PICTURES, "*.png")))
        new_files = sorted(after - before)
        if len(new_files) != 1:
            print(f"FAIL 新增文件数量异常：{new_files}")
            failed = True
            return 1

        path = new_files[0]
        with Image.open(path) as image:
            size = image.size
            extrema = image.convert("L").getextrema()
        print(f"captured file: {path}")
        print(f"image size: {size[0]}x{size[1]} luminance extrema: {extrema}")

        if size != (400, 200):
            print(f"FAIL 输出尺寸不符，期望 400x200，实际 {size[0]}x{size[1]}")
            failed = True
            return 1
        if extrema[0] == extrema[1]:
            print("FAIL 输出为纯色图像")
            failed = True
            return 1

        print("PASS 区域截图端到端测试通过")
        return 0
    finally:
        window = find_window(MAIN_CLASS)
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            process.kill()
        if not failed:
            print("程序已正常退出")


if __name__ == "__main__":
    raise SystemExit(main())
