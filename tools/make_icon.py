"""生成瞬截 / SwiftSnip 的应用图标（多尺寸 ICO + 预览 PNG）。

图形语义：蓝色圆角底 + 白色选区框与四角手柄，直观表达"框选截图"。
每个尺寸独立绘制，保证 16x16 小图标依然清晰。
"""

import io
import os
import struct

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_PATH = os.path.join(ROOT, "res", "swiftsnip.ico")
PREVIEW_PATH = os.path.join(ROOT, "res", "swiftsnip.png")

CANVAS = 512.0
TOP_COLOR = (30, 136, 229)     # #1E88E5
BOTTOM_COLOR = (21, 101, 192)  # #1565C0


def render(size):
    scale = size / CANVAS
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # 背景渐变
    gradient = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gradient_draw = ImageDraw.Draw(gradient)
    for y in range(size):
        t = y / max(1, size - 1)
        color = tuple(
            round(TOP_COLOR[channel] + (BOTTOM_COLOR[channel] - TOP_COLOR[channel]) * t) for channel in range(3)
        )
        gradient_draw.line([(0, y), (size, y)], fill=color + (255,))

    radius = max(3, round(110 * scale))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    image = Image.composite(gradient, image, mask)

    draw = ImageDraw.Draw(image)
    line = max(2, round(22 * scale))
    left = round(120 * scale)
    top = round(156 * scale)
    right = round(392 * scale)
    bottom = round(356 * scale)
    draw.rectangle([left, top, right, bottom], outline=(255, 255, 255, 255), width=line)

    handle = max(3, round(48 * scale))
    half = handle // 2
    for corner in [(left, top), (right, top), (left, bottom), (right, bottom)]:
        draw.rectangle(
            [corner[0] - half, corner[1] - half, corner[0] + handle - half, corner[1] + handle - half],
            fill=(255, 255, 255, 255),
        )

    return image


def pack_ico(images, path):
    blobs = []
    for image in images:
        buffer = io.BytesIO()
        image.save(buffer, format="PNG")
        blobs.append(buffer.getvalue())

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries = b""
    for image, blob in zip(images, blobs):
        width = 0 if image.width >= 256 else image.width
        height = 0 if image.height >= 256 else image.height
        entries += struct.pack("<BBBBHHII", width, height, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(header + entries + b"".join(blobs))


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [render(size) for size in sizes]
    pack_ico(images, ICON_PATH)
    images[-1].save(PREVIEW_PATH)
    print(f"ICO: {ICON_PATH} ({os.path.getsize(ICON_PATH)} bytes, sizes={sizes})")
    print(f"PNG: {PREVIEW_PATH}")


if __name__ == "__main__":
    main()
