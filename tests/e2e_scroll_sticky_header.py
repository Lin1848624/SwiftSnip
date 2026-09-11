"""长截图 · 固定表头场景测试（复现真实网页问题）。

构造与真实网页一致的场景：
- 窗口顶部有固定不动的导航栏（不随滚动移动）
- 下方是深色低对比度、内容稀疏的滚动区域（模拟深色主题网页）
- 选区覆盖整个窗口（包含固定导航栏）

期望：长图应包含完整的滚动内容（高度 ≈ 视口滚动范围 + 选区高度），
而不是只截到一屏就提前结束。
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
TARGET_IMAGE = r"D:\project\SwiftSnip\build\scroll_sticky_target.png"

TOTAL_HEIGHT = 3000
CANVAS_VIEW = 520
WINDOW_HEIGHT = 600
SELECTION = (150, 150, 850, 750)  # 屏幕坐标（左, 上, 右, 下）：覆盖整个窗口

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


def build_target_image(path):
    """深色低对比内容：深灰背景 + 稀疏卡片与文字线条 + 左侧渐变标识条。

    每个 100 像素区块的内容都不同（线条长度、位置、装饰点位置随序号变化），
    避免周期性重复内容干扰模板匹配，贴近真实网页。
    """
    image = Image.new("RGB", (800, TOTAL_HEIGHT), (13, 13, 13))
    draw = ImageDraw.Draw(image)
    for index in range(TOTAL_HEIGHT // 100):
        base = index * 100
        # 区块顶部渐变色标（全宽 8 像素）：蓝通道随序号递增，用于校验拼接顺序
        draw.rectangle([0, base, 800, base + 8], fill=(20, 20, 20 + int(235 * index / 29)))
        # 卡片
        draw.rectangle([44, base + 16, 760, base + 92], fill=(23, 23, 28))
        # 卡片内的文字线条：长度与纵向位置随序号变化，保证每个区块唯一
        width1 = 80 + (index * 53) % 320
        width2 = 60 + (index * 97) % 240
        width3 = 100 + (index * 31) % 400
        offset1 = (index * 7) % 14
        draw.rectangle([64, base + 24 + offset1, 64 + width1, base + 32 + offset1], fill=(204, 204, 204))
        draw.rectangle([64, base + 42 + offset1, 64 + width2, base + 48 + offset1], fill=(138, 138, 138))
        draw.rectangle([64, base + 60 + offset1, 64 + width3, base + 64 + offset1], fill=(102, 102, 102))
        # 右侧装饰点：位置随序号变化
        dot_x = 700 - (index * 17) % 120
        draw.ellipse([dot_x, base + 40, dot_x + 20, base + 60], fill=(90, 90, 100))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    image.save(path)
    return path


def build_target_window(image_path):
    root = tk.Tk()
    root.title("SwiftSnipStickyTarget")
    root.geometry(f"800x{WINDOW_HEIGHT}+100+100")
    root.attributes("-topmost", True)

    # 固定导航栏：不随滚动移动
    header = tk.Frame(root, height=80, bg="#181820")
    header.pack(fill="x")
    header.pack_propagate(False)
    tk.Label(header, text="固定导航栏 · 仪表盘", bg="#181820", fg="#e8e8e8",
             font=("Microsoft YaHei UI", 14)).pack(pady=22)

    # 滚动区域
    canvas = tk.Canvas(root, width=800, height=CANVAS_VIEW, highlightthickness=0, bg="#0d0d0d")
    canvas.pack(fill="both", expand=True)
    photo = tk.PhotoImage(file=image_path)
    canvas.create_image(0, 0, anchor="nw", image=photo)
    canvas.image = photo
    canvas.config(scrollregion=(0, 0, 800, TOTAL_HEIGHT), yscrollincrement=50)
    canvas.bind("<MouseWheel>", lambda event: canvas.yview_scroll(int(-event.delta / 120), "units"))
    root.update()
    return root


def main():
    pictures = resolve_save_dir()
    image_path = build_target_image(TARGET_IMAGE)
    root = build_target_window(image_path)
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
        canvas = None
        for child in root.winfo_children():
            if child.winfo_class() == "Canvas":
                canvas = child
        if canvas is not None:
            print(f"canvas scrollregion={canvas.cget('scrollregion')} height={canvas.winfo_height()}")
            print(f"window geometry: {root.winfo_x()},{root.winfo_y()} {root.winfo_width()}x{root.winfo_height()}")
        deadline = time.time() + 45
        new_files = []
        last_report = time.time()
        while time.time() < deadline:
            root.update()
            if canvas is not None and time.time() - last_report > 1.0:
                print(f"  t={time.time() - deadline + 120:5.1f}s yview={canvas.yview()}")
                last_report = time.time()
            new_files = sorted(set(glob.glob(os.path.join(pictures, "*.png"))) - before)
            if new_files:
                break
            time.sleep(0.05)
        root.update()

        if not new_files:
            print("FAIL 120 秒内未生成长图")
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
            rgb = image.convert("RGB")
            width, height = rgb.size

        # 期望高度 = 选区高度 600 + 滚动范围 (3000 - 520) = 3080
        expected_min = 2500
        expected_max = 3400
        print(f"captured: {path}")
        print(f"size: {width}x{height} (expected height between {expected_min} and {expected_max})")

        if width != right - left:
            print(f"FAIL 宽度不符，期望 {right - left}")
            return 1
        if not (expected_min <= height <= expected_max):
            print("FAIL 长图高度异常：疑似固定表头导致提前结束（只截到一屏）")
            return 1

        # 找出所有全宽渐变色标行（蓝通道明显高于背景），按连续行分组取峰值
        markers = []
        last_y = None
        for y in range(height):
            red, _, blue = rgb.getpixel((350, y))
            # 只在"色标"上采样：色标红色分量很低（20），而文字线是浅灰（>=102）
            if blue < 35 or red > 40:
                continue
            if last_y is not None and y - last_y <= 4:
                if blue > markers[-1][1]:
                    markers[-1] = (y, blue)
                last_y = y
                continue
            markers.append((y, blue))
            last_y = y

        violations = sum(1 for i in range(1, len(markers)) if markers[i][1] + 8 < markers[i - 1][1])
        print(f"markers detected: {len(markers)} (expected ~{height // 100}), order violations: {violations}")
        if len(markers) < 5:
            print("FAIL 未检测到足够的拼接标记")
            return 1
        if violations > max(1, len(markers) // 20):
            print("FAIL 拼接顺序异常")
            return 1

        print("PASS 固定表头场景长截图完整")
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
