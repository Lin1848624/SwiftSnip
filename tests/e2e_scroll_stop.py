"""长截图手动停止测试。

开始滚动捕获后等待数帧，再按一次长截图热键，应停止滚动并保存已捕获内容。
校验输出文件存在且高度大于单帧、小于完整内容高度。
"""

import ctypes
import glob
import os
import subprocess
import time
import tkinter as tk
from ctypes import wintypes

from PIL import Image, ImageDraw

EXE = r"D:\project\SwiftSnip\build\SwiftSnip.exe"
TOTAL_HEIGHT = 3000
TARGET_IMAGE = r"D:\project\SwiftSnip\build\scroll_target.png"

WM_HOTKEY = 0x0312
WM_LBUTTONDOWN = 0x0201
WM_MOUSEMOVE = 0x0200
WM_LBUTTONUP = 0x0202
WM_KEYDOWN = 0x0100
WM_CLOSE = 0x0010
VK_RETURN = 0x0D

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


def make_lparam(x, y):
    return (y << 16) | (x & 0xFFFF)


def find_window(class_name):
    return user32.FindWindowW(class_name, None)


def wait_window(class_name, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        window = find_window(class_name)
        if window:
            return window
        time.sleep(0.02)
    return 0


def resolve_save_dir():
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


def build_target():
    if not os.path.exists(TARGET_IMAGE):
        image = Image.frombytes("RGB", (800, TOTAL_HEIGHT), os.urandom(800 * TOTAL_HEIGHT * 3))
        draw = ImageDraw.Draw(image)
        for index in range(TOTAL_HEIGHT // 100):
            ratio = index / 29
            draw.rectangle([0, index * 100, 800, index * 100 + 20],
                           fill=(int(255 * (1 - ratio)), 0, int(255 * ratio)))
        image.save(TARGET_IMAGE)

    root = tk.Tk()
    root.title("SwiftSnipScrollTarget")
    root.geometry("800x600+100+100")
    root.attributes("-topmost", True)
    canvas = tk.Canvas(root, width=800, height=600, highlightthickness=0, bg="black")
    canvas.pack(fill="both", expand=True)
    photo = tk.PhotoImage(file=TARGET_IMAGE)
    canvas.create_image(0, 0, anchor="nw", image=photo)
    canvas.image = photo
    canvas.config(scrollregion=(0, 0, 800, TOTAL_HEIGHT), yscrollincrement=50)
    canvas.bind("<MouseWheel>", lambda event: canvas.yview_scroll(int(-event.delta / 120), "units"))
    root.update()
    return root


def main():
    pictures = resolve_save_dir()
    root = build_target()
    before = set(glob.glob(os.path.join(pictures, "*.png")))
    process = subprocess.Popen([EXE])
    exit_code = 1

    try:
        main_wnd = wait_window("SwiftSnipMainWnd")
        if not main_wnd:
            print("FAIL 主窗口未出现")
            return 1

        user32.PostMessageW(main_wnd, WM_HOTKEY, 3, 0)
        overlay = wait_window("SwiftSnipOverlayWnd")
        if not overlay:
            print("FAIL 遮罩窗口未出现")
            return 1

        user32.PostMessageW(overlay, WM_LBUTTONDOWN, 1, make_lparam(150, 150))
        time.sleep(0.15)
        user32.PostMessageW(overlay, WM_MOUSEMOVE, 1, make_lparam(850, 650))
        time.sleep(0.2)
        user32.PostMessageW(overlay, WM_LBUTTONUP, 0, make_lparam(850, 650))
        time.sleep(0.3)
        user32.PostMessageW(overlay, WM_KEYDOWN, VK_RETURN, 0)

        # 让自动滚动运行约 1.6 秒（数帧），然后再次按热键请求停止
        deadline = time.time() + 1.6
        while time.time() < deadline:
            root.update()
            time.sleep(0.05)

        print("发送停止请求（再次触发热键）…")
        user32.PostMessageW(main_wnd, WM_HOTKEY, 3, 0)

        deadline = time.time() + 30
        new_files = []
        while time.time() < deadline:
            root.update()
            new_files = sorted(set(glob.glob(os.path.join(pictures, "*.png"))) - before)
            if new_files:
                break
            time.sleep(0.05)
        root.update()

        if not new_files:
            print("FAIL 停止后未生成文件")
            return 1

        path = new_files[0]
        for attempt in range(15):
            try:
                with Image.open(path) as probe:
                    probe.load()
                break
            except Exception:
                if attempt == 14:
                    print("FAIL 文件长时间不可读")
                    return 1
                time.sleep(0.3)

        with Image.open(path) as image:
            width, height = image.size
        print(f"captured: {path}")
        print(f"size: {width}x{height}")
        if width != 700:
            print("FAIL 宽度不符")
            return 1
        if not (500 < height < 2900):
            print("FAIL 手动停止的高度不在预期区间（应大于单帧 500 且小于完整 2900）")
            return 1

        print("PASS 手动停止后保存了部分长图")
        exit_code = 0
    finally:
        window = find_window("SwiftSnipMainWnd")
        if window:
            user32.PostMessageW(window, WM_CLOSE, 0, 0)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        try:
            root.destroy()
        except tk.TclError:
            pass

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
