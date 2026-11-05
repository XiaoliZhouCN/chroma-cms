#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_ptmr.py —— PTMR HDR 测试色块图生成器

规格（由项目所有者给定）：
    / PTMR：9-16-9-full 的 PNG 测试图
    / 10-bit（存放于 16-bit PNG 容器）
    / 2048 x 2048
    / 8 条 ramp：gray - r - g - b - c - y - m - gray（自上而下）
    / 每条 ramp 65 个 step：0~10000 nits 绝对亮度映射到 0~1023 码，
      等距取 64 个点 + 0 点
    / ramp 水平方向从左到右：0 nits -> 10000 nits

规范来源（红线 10：算法实现处必须注明依据）：
    [1] PQ（感知量化）传递函数：SMPTE ST 2084:2014；
        ITU-R BT.2100-2 Table 4 给出同一组公式。
            EOTF:  Y = 10000 * [ max(N^(1/m2) - c1, 0) / (c2 - c3 * N^(1/m2)) ] ^ (1/m1)
            OETF:  N = [ (c1 + c2 * (Y/10000)^m1) / (1 + c3 * (Y/10000)^m1) ] ^ m2
            m1 = 2610/16384            m2 = 2523/4096 * 128
            c1 = 3424/4096             c2 = 2413/4096 * 32     c3 = 2392/4096 * 32
        N 为归一化编码值（0~1），Y 为绝对亮度（cd/m^2，0~10000）。
    [2] BT.2020 原色 + D65 白点：ITU-R BT.2020-2 Table 2。
    [3] PNG 第三版（ISO/IEC 15948:2025）新增的 HDR 相关 chunk：
        cICP（4 字节，ITU-T H.273 code point）、mDCv、cLLi。
        本工具只写 cICP，字段取素材声明的 flavour：
            原色域 colour_primaries        = 9   （BT.2020）
            转换函数 transfer_characteristics = 16  （PQ）
            矩阵 matrix_coefficients       = 9   （BT.2020 NCL，见下方注意）
            range video_full_range_flag    = 1   （full range）
        —— 这正是文件名里 "9-16-9-full" 的来源。

        注意（必须知道的一处语义冲突）：PNG 的像素始终是 RGB，没有 YCbCr，
        所以 cICP 的 matrix 字段严格来说应写 0（identity）。本工具默认写 9，
        目的是让素材的 cICP 与它声明的 flavour 完全一致（便于测试"读取 9-16-9
        标记"这条解析路径）；若你的读取端按 RGB 语义要求 identity，
        加 --cicp-matrix 0 即可。

存储格式的显式约定（重要，不是实现细节）：
    PNG 没有 10-bit 通道，所以 10-bit 码值放进 16-bit 通道：
        --storage shift（默认）  value16 = code10 << 6
            低 6 位恒为 0，读回时 (value16 >> 6) 精确还原 10-bit 码；
            但若读端把它当"满量程 16-bit"用 value16/65535 归一化，会得到
            code/1023 * 65472/65535（满量程处 0.99904，偏差 9.6e-4）。
        --storage normalize      value16 = round(code10 * 65535 / 1023)
            归一化后与 10-bit 码严格同值（满量程 = 65535），
            但低 6 位不再是 0，无法用"看二进制"的方式直接读码。
    两种都满足 (value16 >> 6) == code10（已由 --verify 断言）。

用法：
    python tools/gen_ptmr.py                       # 默认输出到 tools/out/
    python tools/gen_ptmr.py --domain nits         # 等距采样放在 nits 域
    python tools/gen_ptmr.py --preview             # 额外输出 8-bit 预览图（便于肉眼查）
    python tools/gen_ptmr.py --out D:/tmp/x.png --no-csv

注意：本脚本只用 Python 标准库（zlib 手写 PNG 编码），不引入 Pillow 等依赖，
      因此产物字节完全由本文件决定，可复现（PNG 无时间戳 chunk）。
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import zlib
from pathlib import Path

# ---------------------------------------------------------------------------
# 1. PQ（SMPTE ST 2084）—— 见文件头 [1]
# ---------------------------------------------------------------------------

PQ_M1 = 2610.0 / 16384.0
PQ_M2 = (2523.0 / 4096.0) * 128.0
PQ_C1 = 3424.0 / 4096.0
PQ_C2 = (2413.0 / 4096.0) * 32.0
PQ_C3 = (2392.0 / 4096.0) * 32.0

PQ_PEAK_NITS = 10000.0
CODE_MAX_10BIT = 1023


def pq_eotf(normalized_code: float) -> float:
    """归一化 PQ 编码值 N∈[0,1] -> 绝对亮度（cd/m^2）。"""
    n = max(0.0, min(1.0, normalized_code))
    p = n ** (1.0 / PQ_M2)
    numerator = max(p - PQ_C1, 0.0)
    denominator = PQ_C2 - PQ_C3 * p
    return PQ_PEAK_NITS * (numerator / denominator) ** (1.0 / PQ_M1)


def pq_oetf(nits: float) -> float:
    """绝对亮度（cd/m^2） -> 归一化 PQ 编码值 N∈[0,1]。"""
    y = max(0.0, min(PQ_PEAK_NITS, nits)) / PQ_PEAK_NITS
    ym1 = y ** PQ_M1
    return ((PQ_C1 + PQ_C2 * ym1) / (1.0 + PQ_C3 * ym1)) ** PQ_M2


# ---------------------------------------------------------------------------
# 2. ramp step 生成
# ---------------------------------------------------------------------------

def build_steps(steps: int, domain: str) -> list[dict]:
    """生成 steps 个 step 的 (code10, nits) 列表。

    domain == "code"：在 0~1023 码域等距取点（PQ 是感知均匀的，故视觉步距均匀）
    domain == "nits"：在 0~10000 nits 亮度域等距取点（暗部码值跨越大）
    两种都满足"64 个等距点 + 0 点"，区别只在等距发生在哪个轴。
    """
    if steps < 2:
        raise ValueError("steps 至少为 2")

    intervals = steps - 1  # 64
    out: list[dict] = []

    for k in range(steps):
        if domain == "code":
            code = round(CODE_MAX_10BIT * k / intervals)
            nits = pq_eotf(code / CODE_MAX_10BIT)
        elif domain == "nits":
            nits = PQ_PEAK_NITS * k / intervals
            code = round(CODE_MAX_10BIT * pq_oetf(nits))
        else:  # pragma: no cover - argparse 已限制
            raise ValueError(f"未知 domain: {domain}")

        code = max(0, min(CODE_MAX_10BIT, code))
        out.append(
            {
                "step": k,
                "code10": code,
                "nits": nits,
                "normalized10": code / CODE_MAX_10BIT,
            }
        )

    # 同一个 code 重复出现会让"65 个 step"名不副实，必须显式失败
    codes = [s["code10"] for s in out]
    if len(set(codes)) != len(codes):
        raise RuntimeError(f"{domain} 域等距采样产生了重复码值：{codes}")
    return out


# ---------------------------------------------------------------------------
# 3. 颜色与像素
# ---------------------------------------------------------------------------

# (R,G,B) 中哪些通道取 ramp 当前 step 的码值；其余通道固定为 0
RAMP_CHANNELS = {
    "gray": (0, 1, 2),
    "r": (0,),
    "g": (1,),
    "b": (2,),
    "c": (1, 2),
    "y": (0, 1),
    "m": (0, 2),
}

DEFAULT_RAMPS = "gray,r,g,b,c,y,m,gray"


def ramp_pixel(ramp: str, count: int, storage: str) -> tuple[int, int, int]:
    if ramp not in RAMP_CHANNELS:
        raise ValueError(f"未知 ramp 名称: {ramp}（可选 {sorted(RAMP_CHANNELS)}）")

    if storage == "shift":
        v = count << 6
    else:  # normalize
        v = int(round(count * 65535.0 / CODE_MAX_10BIT))

    px = [0, 0, 0]
    for idx in RAMP_CHANNELS[ramp]:
        px[idx] = v
    return (px[0], px[1], px[2])


DEFAULT_CICP_PRIMARIES = 9   # BT.2020
DEFAULT_CICP_TRANSFER = 16   # PQ
DEFAULT_CICP_MATRIX = 9      # BT.2020 NCL（"9-16-9" 的第三个 9）
DEFAULT_CICP_FULL_RANGE = 1  # full range


def make_cicp(primaries: int, transfer: int, matrix: int, full_range: int) -> bytes:
    """ITU-T H.273 cICP 四元组 -> PNG cICP chunk payload。"""
    for name, v in (("primaries", primaries), ("transfer", transfer),
                    ("matrix", matrix), ("full_range", full_range)):
        if not 0 <= v <= 255:
            raise ValueError(f"cICP {name}={v} 超出 0..255")
    return bytes([primaries, transfer, matrix, full_range])


def bar_bounds(width: int, steps: int) -> list[int]:
    """65 个 step 在水平方向上的 66 个边界（整数，宽高不一致时自动 31/32 px）。"""
    return [int(round(k * width / steps)) for k in range(steps + 1)]


def build_16bit_rows(
    width: int, height: int, ramps: list[str], steps: list[dict], storage: str
) -> list[bytes]:
    """生成每行 PNG 数据（不含 filter 字节），每通道 2 字节大端。"""
    bounds = bar_bounds(width, len(steps))
    # 每条 ramp 的一行像素只算一次，然后整段复用
    ramp_rows: list[bytes] = []
    for ramp in ramps:
        row = bytearray(width * 6)
        for i, step in enumerate(steps):
            pattern = struct.pack(">HHH", *ramp_pixel(ramp, step["code10"], storage))
            x0, x1 = bounds[i], bounds[i + 1]
            row[x0 * 6 : x1 * 6] = pattern * (x1 - x0)
        ramp_rows.append(bytes(row))

    # 竖直方向均分：8 条 ramp -> 每条 height/8 行（2048 时正好 256）
    y_bounds = [int(round(i * height / len(ramps))) for i in range(len(ramps) + 1)]
    rows: list[bytes] = []
    for i in range(len(ramps)):
        rows.extend([ramp_rows[i]] * (y_bounds[i + 1] - y_bounds[i]))
    return rows


def build_preview_rows(
    width: int, height: int, ramps: list[str], steps: list[dict]
) -> list[bytes]:
    """8-bit 预览：直接把 10-bit 码除以 4 当 8-bit 用（PQ 感知均匀，观感即"码值视图"）。"""
    bounds = bar_bounds(width, len(steps))
    ramp_rows: list[bytes] = []
    for ramp in ramps:
        row = bytearray(width * 3)
        for i, step in enumerate(steps):
            v = int(round(step["code10"] * 255.0 / CODE_MAX_10BIT))
            px = [0, 0, 0]
            for idx in RAMP_CHANNELS[ramp]:
                px[idx] = v
            pattern = bytes(px)
            x0, x1 = bounds[i], bounds[i + 1]
            row[x0 * 3 : x1 * 3] = pattern * (x1 - x0)
        ramp_rows.append(bytes(row))

    y_bounds = [int(round(i * height / len(ramps))) for i in range(len(ramps) + 1)]
    rows: list[bytes] = []
    for i in range(len(ramps)):
        rows.extend([ramp_rows[i]] * (y_bounds[i + 1] - y_bounds[i]))
    return rows


# ---------------------------------------------------------------------------
# 4. 极简 PNG 编码器（RGB，8 或 16 bit，filter=0）
# ---------------------------------------------------------------------------

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def _chunk(tag: bytes, data: bytes) -> bytes:
    return (
        struct.pack(">I", len(data))
        + tag
        + data
        + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    )


def write_png(
    path: Path,
    width: int,
    height: int,
    bit_depth: int,
    rows: list[bytes],
    cicp: bytes | None = None,
) -> int:
    """写 RGB PNG。bit_depth ∈ {8, 16}；cicp 仅在 16-bit 主图上传入。"""
    if len(rows) != height:
        raise ValueError(f"行数不匹配: {len(rows)} != {height}")
    expected = width * (3 if bit_depth == 8 else 6)
    for i, r in enumerate(rows):
        if len(r) != expected:
            raise ValueError(f"第 {i} 行字节数 {len(r)} != {expected}")

    color_type = 2  # truecolor，无 alpha
    ihdr = struct.pack(">IIBBBBB", width, height, bit_depth, color_type, 0, 0, 0)

    raw = bytearray(len(rows) * (expected + 1))
    for i, r in enumerate(rows):
        base = i * (expected + 1)
        raw[base] = 0  # filter type 0 (None)
        raw[base + 1 : base + 1 + expected] = r

    buf = bytearray(PNG_SIGNATURE)
    buf += _chunk(b"IHDR", ihdr)
    if cicp is not None:
        buf += _chunk(b"cICP", cicp)
    buf += _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    buf += _chunk(b"IEND", b"")
    path.write_bytes(buf)
    return len(buf)


# ---------------------------------------------------------------------------
# 5. 自检：重新解析产物，逐 bar 校验码值
# ---------------------------------------------------------------------------

def _parse_png(data: bytes) -> tuple[dict, bytes, dict[bytes, bytes]]:
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("不是 PNG")
    pos = len(PNG_SIGNATURE)
    ihdr: dict = {}
    idat = bytearray()
    aux: dict[bytes, bytes] = {}
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos : pos + 4])
        tag = data[pos + 4 : pos + 8]
        payload = data[pos + 8 : pos + 8 + length]
        (crc,) = struct.unpack(">I", data[pos + 8 + length : pos + 12 + length])
        if crc != (zlib.crc32(tag + payload) & 0xFFFFFFFF):
            raise ValueError(f"chunk {tag!r} CRC 校验失败")
        pos += 12 + length
        if tag == b"IHDR":
            w, h, bd, ct, comp, filt, inter = struct.unpack(">IIBBBBB", payload)
            ihdr = dict(width=w, height=h, bit_depth=bd, color_type=ct,
                        compression=comp, filter=filt, interlace=inter)
        elif tag == b"IDAT":
            idat += payload
        elif tag == b"IEND":
            break
        else:
            aux[tag] = payload
    return ihdr, bytes(idat), aux


def verify_png(
    path: Path,
    steps: list[dict],
    ramps: list[str],
    width: int,
    height: int,
    storage: str,
    cicp: bytes | None,
) -> None:
    data = path.read_bytes()
    ihdr, idat, aux = _parse_png(data)
    assert ihdr["width"] == width and ihdr["height"] == height, ihdr
    assert ihdr["bit_depth"] == 16 and ihdr["color_type"] == 2, ihdr
    assert ihdr["compression"] == 0 and ihdr["filter"] == 0 and ihdr["interlace"] == 0

    raw = zlib.decompress(idat)
    stride = 1 + width * 6
    assert len(raw) == stride * height, (len(raw), stride * height)
    assert all(raw[y * stride] == 0 for y in range(height)), "出现了非 0 filter"

    bounds = bar_bounds(width, len(steps))
    y_bounds = [int(round(i * height / len(ramps))) for i in range(len(ramps) + 1)]

    for ri, ramp in enumerate(ramps):
        y = (y_bounds[ri] + y_bounds[ri + 1]) // 2
        row = raw[y * stride + 1 : (y + 1) * stride]
        for i, step in enumerate(steps):
            x = (bounds[i] + bounds[i + 1]) // 2
            got = struct.unpack(">HHH", row[x * 6 : x * 6 + 6])
            want = ramp_pixel(ramp, step["code10"], storage)

            # 16-bit 值逐字节相等，且右移 6 位后精确还原 10-bit 码
            # （shift / normalize 两种 storage 都满足 (v >> 6) == code10）
            for c in range(3):
                want_code = step["code10"] if c in RAMP_CHANNELS[ramp] else 0
                got_code = got[c] >> 6
                if got_code != want_code:
                    raise AssertionError(
                        f"{ramp} step {i}: 通道 {c} 码值 {got_code} != 期望 {want_code}"
                    )
                if got[c] != want[c]:
                    raise AssertionError(
                        f"{ramp} step {i}: 通道 {c} 16-bit 值 {got[c]} != 期望 {want[c]}"
                    )

    # 可选 chunk 校验
    if cicp is not None:
        assert b"cICP" in aux, "缺少 cICP chunk"
        assert aux[b"cICP"] == cicp, (aux[b"cICP"], cicp)
    else:
        assert b"cICP" not in aux, "不该出现 cICP chunk"


# ---------------------------------------------------------------------------
# 6. CSV（可追溯性：每个 step 的码值与 nits 对照）
# ---------------------------------------------------------------------------

def write_csv(path: Path, steps: list[dict], ramps: list[str], storage: str) -> None:
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow([
            "ramp", "step", "code10", "normalized10", "nits_cd_m2",
            "value16", "normalized16",
        ])
        for ramp in ramps:
            for s in steps:
                v16 = (s["code10"] << 6) if storage == "shift" else int(
                    round(s["code10"] * 65535.0 / CODE_MAX_10BIT)
                )
                w.writerow([
                    ramp, s["step"], s["code10"],
                    f"{s['normalized10']:.9f}", f"{s['nits']:.6f}",
                    v16, f"{v16 / 65535.0:.9f}",
                ])


# ---------------------------------------------------------------------------
# 7. CLI
# ---------------------------------------------------------------------------

def parse_args(argv: list[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="生成 PTMR HDR 测试色块图（PQ / BT.2020 / 10-bit in 16-bit PNG）",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--out", type=Path,
                   default=Path(__file__).resolve().parent / "out" /
                   "PTMR_9-16-9-full_2048x2048_10bit.png",
                   help="输出 PNG 路径")
    p.add_argument("--width", type=int, default=2048)
    p.add_argument("--height", type=int, default=2048)
    p.add_argument("--steps", type=int, default=65, help="每条 ramp 的 step 数")
    p.add_argument("--ramps", default=DEFAULT_RAMPS,
                   help="自上而下的 ramp 列表，逗号分隔")
    p.add_argument("--domain", choices=["code", "nits"], default="code",
                   help="等距采样发生在哪个轴：PQ 码域（视觉均匀）或 nits 域")
    p.add_argument("--storage", choices=["shift", "normalize"], default="shift",
                   help="10-bit 码放进 16-bit 通道的方式")
    p.add_argument("--no-cicp", action="store_true",
                   help="不写 cICP chunk（默认写 9/16/9/full）")
    p.add_argument("--cicp-matrix", type=int, choices=[0, 9], default=DEFAULT_CICP_MATRIX,
                   help="cICP 的 matrix 字段：9=BT.2020 NCL（与 9-16-9 的名称一致，默认）；"
                        "0=identity（PNG 像素是 RGB 时语义上更严格）")
    p.add_argument("--csv", dest="csv", action="store_true", default=True,
                   help="写同名 .csv 对照表（默认写）")
    p.add_argument("--no-csv", dest="csv", action="store_false")
    p.add_argument("--preview", action="store_true", help="额外输出 8-bit 预览 PNG")
    p.add_argument("--no-verify", action="store_true", help="跳过产物自检")
    return p.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    if args.width <= 0 or args.height <= 0:
        print("error: width/height must be positive", file=sys.stderr)
        return 2

    ramps = [r.strip() for r in args.ramps.split(",") if r.strip()]
    if len(ramps) != 8:
        print(f"error: need 8 ramps, got {len(ramps)}", file=sys.stderr)
        return 2
    unknown = [r for r in ramps if r not in RAMP_CHANNELS]
    if unknown:
        print(f"error: unknown ramp {unknown}; valid: {sorted(RAMP_CHANNELS)}",
              file=sys.stderr)
        return 2

    steps = build_steps(args.steps, args.domain)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    rows = build_16bit_rows(args.width, args.height, ramps, steps, args.storage)
    cicp = None if args.no_cicp else make_cicp(
        DEFAULT_CICP_PRIMARIES, DEFAULT_CICP_TRANSFER, args.cicp_matrix,
        DEFAULT_CICP_FULL_RANGE)
    size = write_png(args.out, args.width, args.height, 16, rows, cicp=cicp)

    if args.csv:
        csv_path = args.out.with_suffix(".csv")
        write_csv(csv_path, steps, ramps, args.storage)

    if args.preview:
        preview_path = args.out.with_name(args.out.stem + "_preview8.png")
        write_png(preview_path, args.width, args.height, 8,
                  build_preview_rows(args.width, args.height, ramps, steps))

    if not args.no_verify:
        verify_png(args.out, steps, ramps, args.width, args.height, args.storage, cicp)

    # 控制台摘要保持 ASCII：Windows PowerShell 5.1 / 非 UTF-8 代码页下中文会乱码
    # （与 README「已知问题 ④」同一类问题）。脚本源码本身是 UTF-8，Python 自带解码。
    print(f"output : {args.out}  ({size / 1024 / 1024:.1f} MiB)")
    print(f"size   : {args.width} x {args.height}, 8 ramps x {args.steps} steps")
    print(f"ramps  : {' - '.join(ramps)}")
    print(f"domain : {args.domain}   storage: {args.storage}")
    print(f"cICP   : {'/'.join(str(b) for b in cicp) if cicp else 'none'}"
          f"  (primaries/transfer/matrix/range)"
          + ("" if args.no_cicp else
             "  [matrix=9 matches the 9-16-9 name; use --cicp-matrix 0 for identity]"))
    print(f"code10 : {steps[0]['code10']} .. {steps[-1]['code10']}")
    print(f"nits   : {steps[0]['nits']:.3f} .. {steps[-1]['nits']:.3f}")
    print("  step  code10      nits")
    for s in (steps[:4] + steps[-3:]):
        print(f"  {s['step']:4d}  {s['code10']:6d}  {s['nits']:9.3f}")
    if not args.no_verify:
        print("verify : OK (chunk CRC / row stride / 10-bit code of every bar re-read)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
