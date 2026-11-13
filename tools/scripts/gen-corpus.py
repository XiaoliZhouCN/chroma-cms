# -*- coding: utf-8 -*-
"""生成 tests/corpus 的小样本（开发依赖 Pillow；生成结果入库，构建时不需 Pillow）。

用法（仓库根目录）：
    python tools/scripts/gen-corpus.py
"""
import os
import struct
import zlib

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CORPUS = os.path.join(ROOT, "tests", "corpus")
W = H = 64


def gradient_rgb() -> bytes:
    data = bytearray()
    for y in range(H):
        for x in range(W):
            data += bytes(((x * 4) % 256, (y * 4) % 256, ((x + y) * 2) % 256))
    return bytes(data)


def gradient_rgba() -> bytes:
    rgb = gradient_rgb()
    data = bytearray()
    for i in range(0, len(rgb), 3):
        data += rgb[i:i + 3] + b"\xff"
    return bytes(data)


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(path: str, bit_depth: int, color_type: int, rows: list, extra=()) -> None:
    ihdr = struct.pack(">IIBBBBB", W, H, bit_depth, color_type, 0, 0, 0)
    raw = b"".join(b"\x00" + row for row in rows)
    png = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", ihdr)
    for kind, data in extra:
        png += png_chunk(kind, data)
    png += png_chunk(b"IDAT", zlib.compress(raw, 9))
    png += png_chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def write_exr(path: str, width: int = 8, height: int = 8) -> None:
    def attr(name: str, kind: str, data: bytes) -> bytes:
        return name.encode() + b"\x00" + kind.encode() + b"\x00" + struct.pack("<i", len(data)) + data

    channels = b""
    for name in (b"B", b"G", b"R"):
        channels += name + b"\x00" + struct.pack("<i", 1)  # HALF
        channels += b"\x00" + b"\x00\x00\x00"               # pLinear + reserved
        channels += struct.pack("<ii", 1, 1)                # x/y sampling
    channels += b"\x00"

    box = struct.pack("<iiii", 0, 0, width - 1, height - 1)
    header = b""
    header += attr("channels", "chlist", channels)
    header += attr("compression", "compression", b"\x00")
    header += attr("dataWindow", "box2i", box)
    header += attr("displayWindow", "box2i", box)
    header += attr("lineOrder", "lineOrder", b"\x00")
    header += attr("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    header += attr("screenWindowCenter", "v2f", struct.pack("<ff", 0.0, 0.0))
    header += attr("screenWindowWidth", "float", struct.pack("<f", 1.0))
    header += b"\x00"

    offset_table_size = 8 * height
    data_offset = 4 + len(header) + offset_table_size
    offsets = []
    chunks = b""
    for y in range(height):
        payload = b""
        for x in range(width):
            payload += struct.pack("<eee", 0.0, 0.0, (x + y) / (width + height))
        chunk = struct.pack("<ii", y, len(payload)) + payload
        offsets.append(data_offset + len(chunks))
        chunks += chunk

    with open(path, "wb") as f:
        f.write(struct.pack("<I", 20000630))
        f.write(struct.pack("<I", 2))
        f.write(header)
        for offset in offsets:
            f.write(struct.pack("<Q", offset))
        f.write(chunks)


def main() -> None:
    for sub in ("png", "jpeg", "tiff", "gif", "webp", "exr"):
        os.makedirs(os.path.join(CORPUS, sub), exist_ok=True)

    rgb_image = Image.frombytes("RGB", (W, H), gradient_rgb())

    # PNG 自定义写入，精确控制色彩 chunk。
    write_png(os.path.join(CORPUS, "png", "rgba8-plain.png"), 8, 6,
              [gradient_rgba()[y * W * 4:(y + 1) * W * 4] for y in range(H)])
    write_png(os.path.join(CORPUS, "png", "rgb8-srgb.png"), 8, 2,
              [gradient_rgb()[y * W * 3:(y + 1) * W * 3] for y in range(H)],
              extra=[(b"sRGB", b"\x00")])

    rgba16 = bytearray()
    rgba8 = gradient_rgba()
    for index in range(0, len(rgba8), 4):
        for channel in range(4):
            rgba16 += struct.pack(">H", rgba8[index + channel] * 257)
    write_png(os.path.join(CORPUS, "png", "rgba16.png"), 16, 6,
              [bytes(rgba16[y * W * 4 * 2:(y + 1) * W * 4 * 2]) for y in range(H)])

    rgb_image.save(os.path.join(CORPUS, "jpeg", "rgb8-no-icc.jpg"), quality=90)

    rgb_image.save(os.path.join(CORPUS, "tiff", "rgb8-single.tif"))
    rgb_image.save(os.path.join(CORPUS, "tiff", "rgb8-multipage.tif"),
                   save_all=True, append_images=[rgb_image.transpose(Image.FLIP_LEFT_RIGHT)])

    rgb_image.save(os.path.join(CORPUS, "gif", "static.gif"))
    rgb_image.save(os.path.join(CORPUS, "gif", "animated.gif"),
                   save_all=True, append_images=[rgb_image.rotate(180)], duration=100, loop=0)

    rgb_image.save(os.path.join(CORPUS, "webp", "rgb8.webp"), quality=90)

    write_exr(os.path.join(CORPUS, "exr", "rgb-half.exr"))

    print("corpus written to", CORPUS)


if __name__ == "__main__":
    main()
