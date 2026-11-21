#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 8 ramp × 17 色块 PTMR 测试图和坐标 JSON。"""
from __future__ import annotations

import argparse
import importlib.util
import json
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("gen_ptmr", HERE / "gen_ptmr.py")
gp = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(gp)

RAMP_NAMES = ["gray", "red", "green", "blue", "cyan", "yellow", "magenta", "gray2"]
GP_RAMPS = ["gray", "r", "g", "b", "c", "y", "m", "gray"]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument("--width", type=int, default=2048)
    ap.add_argument("--height", type=int, default=2048)
    ap.add_argument("--steps", type=int, default=17)
    ap.add_argument("--screen-size", default="2560,1440")
    ap.add_argument("--available-rect", default="0,30,2560,1410")
    args = ap.parse_args()
    ax, ay, aw, ah = [int(v) for v in args.available_rect.split(",")]
    sw, sh = [int(v) for v in args.screen_size.split(",")]

    steps = gp.build_steps(args.steps, "code")
    bounds = gp.bar_bounds(args.width, len(steps))
    ybounds = [round(i * args.height / len(GP_RAMPS)) for i in range(len(GP_RAMPS) + 1)]
    rows: list[bytes] = []
    for ri, ramp in enumerate(GP_RAMPS):
        row = bytearray(args.width * 6)
        for i, step in enumerate(steps):
            pattern = struct.pack(">HHH", *gp.ramp_pixel(ramp, step["code10"], "shift"))
            start, end = bounds[i], bounds[i + 1]
            row[start * 6 : end * 6] = pattern * (end - start)
        rows.extend([bytes(row)] * (ybounds[ri + 1] - ybounds[ri]))
    assert len(rows) == args.height

    cicp = gp.make_cicp(gp.DEFAULT_CICP_PRIMARIES, gp.DEFAULT_CICP_TRANSFER,
                        gp.DEFAULT_CICP_MATRIX, gp.DEFAULT_CICP_FULL_RANGE)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    gp.write_png(args.out, args.width, args.height, 16, rows, cicp)

    scale = min(aw / args.width, ah / args.height)
    disp_w = args.width * scale
    disp_h = args.height * scale
    left = ax + (aw - disp_w) * 0.5
    top = ay + (ah - disp_h) * 0.5

    patches = []
    for ri, name in enumerate(RAMP_NAMES):
        cy = (ybounds[ri] + ybounds[ri + 1]) * 0.5
        sy = top + (ri + 0.5) * disp_h / len(GP_RAMPS)
        for i in range(len(steps)):
            sx = (bounds[i] + bounds[i + 1]) * 0.5
            px = left + (i + 0.5) * disp_w / len(steps)
            patches.append({
                "name": f"{name}-{i:02d}",
                "image": {"x": round(sx, 3), "y": round(cy, 3)},
                "screen": {"x": round(px, 3), "y": round(sy, 3)},
            })
    doc = {
        "image": args.out.name,
        "imagePath": str(args.out),
        "imageSize": {"width": args.width, "height": args.height},
        "ramps": RAMP_NAMES,
        "steps": args.steps,
        "screenSize": {"width": sw, "height": sh},
        "availableRect": {"x": ax, "y": ay, "width": aw, "height": ah},
        "displayScale": round(scale, 9),
        "patches": patches,
    }
    args.json.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"image: {args.out} ({args.width}x{args.height})")
    print(f"json : {args.json} ({len(patches)} patches)")
    print(f"scale: {scale:.9f}  display: {disp_w:.3f}x{disp_h:.3f}  left/top: {left:.3f}/{top:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
