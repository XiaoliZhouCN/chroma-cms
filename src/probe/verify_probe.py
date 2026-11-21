#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 Probe 的完整性和数值正确性。

Probe 输出是 Stage B：present 后 renderer offscreen surface 的 raw linear 值。
本脚本独立解析测试图，按 Skia shader 的 CPU 等价公式生成期望值，再做比较。
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def _png_chunks(data: bytes) -> dict[bytes, bytes]:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("不是 PNG")
    pos = 8
    chunks: dict[bytes, bytes] = {}
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        tag = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        chunks.setdefault(tag, b"")
        chunks[tag] += payload
        if tag == b"IEND":
            break
    return chunks


def load_png(path: Path):
    chunks = _png_chunks(path.read_bytes())
    ihdr = chunks[b"IHDR"]
    width, height, depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", ihdr)
    if (depth, color_type, comp, filt, interlace) != (16, 2, 0, 0, 0):
        raise ValueError("只支持 16-bit RGB PNG")
    raw = zlib.decompress(chunks[b"IDAT"])
    stride = 1 + width * 6
    rows = []
    for y in range(height):
        start = y * stride
        if raw[start] != 0:
            raise ValueError("只支持 filter=0")
        rows.append(raw[start + 1 : start + stride])
    return width, height, rows


def pq_eotf(code: list[float]) -> list[float]:
    m1 = 0.1593017578125
    m2 = 78.84375
    c1 = 0.8359375
    c2 = 18.8515625
    c3 = 18.6875
    p = [max(v, 0.0) ** (1.0 / m2) for v in code]
    numerator = [max(v - c1, 0.0) for v in p]
    denominator = [c2 - c3 * v for v in p]
    return [(numerator[i] / max(denominator[i], 1e-6)) ** (1.0 / m1) for i in range(3)]


def mat3_vec(matrix: list[list[float]], vector: list[float]) -> list[float]:
    return [sum(matrix[i][j] * vector[j] for j in range(3)) for i in range(3)]


# fullscreen.sksl: float3x3 按列初始化；下面是展开后的行。
REC2020_TO_XYZ = [
    [0.636958, 0.144617, 0.168881],
    [0.262700, 0.677998, 0.059302],
    [0.000000, 0.028073, 1.060985],
]
XYZ_TO_P3 = [
    [2.4934969119, -0.9313836179, -0.4027107845],
    [-0.8294889696, 1.7626640603, 0.0236246858],
    [0.0358458302, -0.0761723893, 0.9568845240],
]


def source_rgb(rows: list[bytes], width: int, height: int, x: float, y: float) -> tuple[int, int, int]:
    px = max(0, min(width - 1, int(round(x))))
    py = max(0, min(height - 1, int(round(y))))
    offset = px * 6
    return struct.unpack(">HHH", rows[py][offset : offset + 6])


def expected_linear(pixel: tuple[int, int, int], shifted10: bool) -> list[float]:
    if shifted10:
        code = [(value >> 6) / 1023.0 for value in pixel]
    else:
        code = [value / 65535.0 for value in pixel]
    linear2020 = pq_eotf(code)
    xyz = mat3_vec(REC2020_TO_XYZ, linear2020)
    target = mat3_vec(XYZ_TO_P3, xyz)
    target = [max(v, 0.0) for v in target]
    return [v * 10000.0 / 80.0 for v in target]


def load_config() -> tuple[Path, Path, Path]:
    config_path = HERE / "card_config" / "17_ramp_config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    image = Path(config["image"])
    coords = Path(config["loc_config"])
    # card_config 里的 /assets 是仓库相对路径，不是文件系统根目录。
    image = (REPO / str(image).lstrip("./")).resolve()
    coords = (REPO / str(coords).lstrip("./")).resolve()
    return image, coords, config_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, default=HERE / "output" / "card_color.csv")
    parser.add_argument("--report", type=Path, default=None)
    parser.add_argument("--tolerance", type=float, default=0.2)
    args = parser.parse_args()

    image_path, coords_path, _ = load_config()
    coords = json.loads(coords_path.read_text(encoding="utf-8"))
    width, height, rows = load_png(image_path)
    patches = coords["patches"]

    if not args.csv.exists():
        print(f"错误: CSV 不存在: {args.csv}")
        return 1
    import csv

    with args.csv.open(newline="", encoding="utf-8") as handle:
        got_rows = list(csv.DictReader(handle))

    problems: list[str] = []
    if len(got_rows) != len(patches):
        problems.append(f"行数不匹配: {len(got_rows)} != {len(patches)}")
    if len(got_rows) != 136:
        problems.append(f"patch 数不是 136: {len(got_rows)}")

    # 测试图是 10-bit << 6 存储；ImageLoader 会重映射到 16-bit 满量程。
    shifted10 = True
    for y in range(0, height, 97):
        for x in range(0, width, 31):
            for v in source_rgb(rows, width, height, x, y):
                if v & 0x3F:
                    shifted10 = False
                    break
            if not shifted10:
                break
        if not shifted10:
            break

    max_abs = 0.0
    worst = None
    checked = 0
    for patch, got in zip(patches, got_rows):
        name = patch["name"]
        if got.get("block") != name:
            problems.append(f"block 顺序不匹配: {got.get('block')} != {name}")
            continue
        try:
            actual = [float(got["r"]), float(got["g"]), float(got["b"])]
        except (KeyError, ValueError):
            problems.append(f"{name}: 无法解析 r/g/b")
            continue
        if any(not math.isfinite(v) for v in actual):
            problems.append(f"{name}: 出现非有限值")
            continue

        pixel = source_rgb(rows, width, height, patch["image"]["x"], patch["image"]["y"])
        expected = expected_linear(pixel, shifted10)
        checked += 1
        for channel in range(3):
            diff = abs(actual[channel] - expected[channel])
            if diff > max_abs:
                max_abs = diff
                worst = (name, channel, expected[channel], actual[channel])

    if max_abs > args.tolerance:
        problems.append(
            f"最大绝对误差 {max_abs:.9g} 超过 F16 容差 {args.tolerance:.9g} (worst={worst})")

    report = [
        f"image: {image_path}",
        f"coords: {coords_path}",
        f"csv: {args.csv}",
        f"patches: {len(patches)}",
        f"checked: {checked}",
        f"max_abs_error: {max_abs:.9g}",
        f"worst: {worst}",
        f"status: {'PASS' if not problems else 'FAIL'}",
    ]
    for line in report:
        print(line)
    for problem in problems:
        print("问题:", problem)
    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text("\n".join(report + [f"problem: {p}" for p in problems]) + "\n",
                               encoding="utf-8")
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
