"""长截图端到端测试。

创建一个可滚动的 tkinter 窗口（3000 像素高，随机纹理 + 每 100 像素一条红→蓝渐变标识条），
触发长截图 → 框选该窗口 → 等待自动滚动拼接 → 校验长图尺寸与颜色顺序连续性。
滚动由被测程序通过 SendInput 真实发出，tkinter 窗口真实响应滚轮事件。
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

TOTAL_HEIGHT = 3000
BAND_HEIGHT = 100
SELECTION = (150, 150, 850, 650)  # 屏幕坐标（左, 上, 右, 下）
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


def build_target_image(path):
    """生成带随机纹理的长图内容：噪声保证模板匹配唯一，标识条用于校验拼接顺序。"""
    image = Image.frombytes("RGB", (800, TOTAL_HEIGHT), os.urandom(800 * TOTAL_HEIGHT * 3))
    draw = ImageDraw.Draw(image)
    bands = TOTAL_HEIGHT // BAND_HEIGHT
    for index in range(bands):
        ratio = index / (bands - 1)
        color = (int(255 * (1 - ratio)), 0, int(255 * ratio))
        draw.rectangle([0, index * BAND_HEIGHT, 800, index * BAND_HEIGHT + 20], fill=color)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path)
    return path


def build_target_window(image_path):
    root = tk.Tk()
    root.title("SwiftSnipScrollTarget")
    root.geometry("800x600+100+100")
    root.attributes("-topmost", True)

    canvas = tk.Canvas(root, width=800, height=600, highlightthickness=0, bg="black")
    canvas.pack(fill="both", expand=True)

    # 保持引用，避免 PhotoImage 被回收
    photo = tk.PhotoImage(file=image_path)
    canvas.create_image(0, 0, anchor="nw", image=photo)
    canvas.image = photo

    canvas.config(scrollregion=(0, 0, 800, TOTAL_HEIGHT), yscrollincrement=50)
    canvas.bind("<MouseWheel>", lambda event: canvas.yview_scroll(int(-event.delta / 120), "units"))
    root.update()
    return root


def main():
    image_path = build_target_image(TARGET_IMAGE)
    root = build_target_window(image_path)
    before = set(glob.glob(os.path.join(PICTURES, "*.png")))
    process = subprocess.Popen([EXE])
    exit_code = 1

    try:
        main_wnd = wait_window("SwiftSnipMainWnd")
        if not main_wnd:
            print("FAIL 主窗口未出现")
            return 1

        # 触发长截图（热键 ID = 3）
        user32.PostMessageW(main_wnd, WM_HOTKEY, 3, 0)
        overlay = wait_window("SwiftSnipOverlayWnd")
        if not overlay:
            print("FAIL 遮罩窗口未出现")
            return 1

        left, top, right, bottom = SELECTION
        user32.PostMessageW(overlay, WM_LBUTTONDOWN, 1, make_lparam(left, top))
        time.sleep(0.15)
        user32.PostMessageW(overlay, WM_MOUSEMOVE, 1, make_lparam((left + right) // 2, (top + bottom) // 2))
        time.sleep(0.15)
        user32.PostMessageW(overlay, WM_MOUSEMOVE, 1, make_lparam(right, bottom))
        time.sleep(0.2)
        user32.PostMessageW(overlay, WM_LBUTTONUP, 0, make_lparam(right, bottom))
        time.sleep(0.3)
        user32.PostMessageW(overlay, WM_KEYDOWN, VK_RETURN, 0)

        print("滚动捕获已开始，等待长图生成…")
        deadline = time.time() + 90
        new_files = []
        while time.time() < deadline:
            root.update()
            new_files = sorted(set(glob.glob(os.path.join(PICTURES, "*.png"))) - before)
            if new_files:
                break
            time.sleep(0.05)
        root.update()

        if not new_files:
            print("FAIL 90 秒内未生成长图")
            return 1

        path = new_files[0]
        print(f"captured: {path}")
        if "_Long_" not in os.path.basename(path):
            print("FAIL 文件名缺少 _Long_ 标识")
            return 1

        # 大图编码需要时间，等待文件可完整读取
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
            rgb = image.convert("RGB")
            width, height = rgb.size
            print(f"size: {width}x{height}")

            if width != right - left:
                print(f"FAIL 宽度不符，期望 {right - left}")
                return 1
            if not (2000 <= height <= 3200):
                print("FAIL 长图高度不在预期范围（2000-3200）")
                return 1

            # 找出标识条（横向纯色行），检查蓝通道沿垂直方向单调递增
            band_positions = []
            for y in range(0, height, 4):
                column = [rgb.getpixel((x, y))[2] for x in range(0, width, 40)]
                mean_blue = sum(column) / len(column)
                variance = sum((value - mean_blue) ** 2 for value in column) / len(column)
                if variance < 15:
                    band_positions.append((y, mean_blue))

            # 相邻行合并为一条标识带
            merged = []
            last_y = None
            for y, blue in band_positions:
                if last_y is not None and y - last_y <= 8:
                    last_y = y
                    continue
                merged.append((y, blue))
                last_y = y

            violations = sum(1 for i in range(1, len(merged)) if merged[i][1] + 6 < merged[i - 1][1])
            print(f"marker bands detected: {len(merged)} (expected ~{height // BAND_HEIGHT}), "
                  f"order violations: {violations}")
            if len(merged) < 5:
                print("FAIL 未检测到足够的标识带，内容可能拼接异常")
                return 1
            if violations > max(1, len(merged) // 20):
                print("FAIL 拼接顺序异常（标识带颜色出现回退，疑似重复或错位）")
                return 1

        print("PASS 长截图自动滚动与拼接正常")
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
