"""校验自检截图：解析 PNG 结构并检查内容不是纯色空白。

用法：python tests/verify_capture.py <png 路径> [期望宽度 期望高度]
"""

import struct
import sys
import zlib


def parse_png(path):
    with open(path, "rb") as handle:
        data = handle.read()

    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("不是有效的 PNG 文件")

    position = 8
    header = None
    idat = b""
    while position + 12 <= len(data):
        length = struct.unpack(">I", data[position:position + 4])[0]
        chunk_type = data[position + 4:position + 8]
        chunk = data[position + 8:position + 8 + length]
        if chunk_type == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk[:13])
        elif chunk_type == b"IDAT":
            idat += chunk
        position += 12 + length
        if chunk_type == b"IEND":
            break

    if header is None or not idat:
        raise ValueError("PNG 缺少 IHDR 或 IDAT 数据")

    width, height, bit_depth, color_type = header[0], header[1], header[2], header[3]
    raw = zlib.decompress(idat)
    step = max(1, len(raw) // 200000)
    sample = raw[::step]
    unique_values = len(set(sample))
    average = sum(sample) / len(sample)
    return width, height, bit_depth, color_type, unique_values, average


def main():
    if len(sys.argv) < 2:
        print("用法：python tests/verify_capture.py <png 路径> [期望宽度 期望高度]")
        return 2

    path = sys.argv[1]
    width, height, bit_depth, color_type, unique_values, average = parse_png(path)

    if len(sys.argv) >= 4:
        expected_width = int(sys.argv[2])
        expected_height = int(sys.argv[3])
        if width != expected_width or height != expected_height:
            print(f"FAIL 尺寸不符：实际 {width}x{height}，期望 {expected_width}x{expected_height}")
            return 1

    # 全黑/全白等纯色画面的抽样值会非常单一，视为捕获异常
    if unique_values <= 2:
        print(f"FAIL 画面疑似纯色：unique={unique_values} average={average:.1f}")
        return 1

    print(f"OK size={width}x{height} bit_depth={bit_depth} color_type={color_type} "
          f"sample_unique={unique_values} sample_average={average:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
