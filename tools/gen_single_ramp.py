#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成单 ramp / 17 色块 PTMR 测试图，并输出坐标 JSON。

沿用 gen_ptmr.py 的 PTMR 规格：
  PQ / BT.2020 / 10-bit in 16-bit PNG / cICP 9-16-9-full
"""
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


def parse_rect(text: str) -> tuple[int, int, int, int]:
    parts = [int(x) for x in text.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("rect 需要 x,y,w,h")
    return tuple(parts)  # type: ignore[return-value]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--json", type=Path, required=True)
    ap.add_argument("--width", type=int, default=2048)
    ap.add_argument("--height", type=int, default=2048)
    ap.add_argument("--steps", type=int, default=17)
    ap.add_argument("--ramp", default="gray")
    ap.add_argument("--storage", choices=["shift", "normalize"], default="shift")
    ap.add_argument("--screen-size", default="2560,1440")
    ap.add_argument("--available-rect", default="0,30,2560,1410",
                    help="屏幕可用区，top-left 坐标 x,y,w,h")
    args = ap.parse_args()

    steps = gp.build_steps(args.steps, "code")
    bounds = gp.bar_bounds(args.width, len(steps))
    rows: list[bytes] = []
    for _ in range(args.height):
        row = bytearray(args.width * 6)
        for i, step in enumerate(steps):
            pattern = struct.pack(">HHH", *gp.ramp_pixel(args.ramp, step["code10"], args.storage))
            start, end = bounds[i], bounds[i + 1]
            row[start * 6 : end * 6] = pattern * (end - start)
        rows.append(bytes(row))

    cicp = gp.make_cicp(gp.DEFAULT_CICP_PRIMARIES, gp.DEFAULT_CICP_TRANSFER,
                        gp.DEFAULT_CICP_MATRIX, gp.DEFAULT_CICP_FULL_RANGE)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    gp.write_png(args.out, args.width, args.height, 16, rows, cicp)

    screen_w, screen_h = [int(x) for x in args.screen_size.split(",")]
    ax, ay, aw, ah = parse_rect(args.available_rect)
    scale = min(aw / args.width, ah / args.height)
    disp_w = args.width * scale
    disp_h = args.height * scale
    left = ax + (aw - disp_w) * 0.5
    top = ay + (ah - disp_h) * 0.5

    patches = []
    for i, step in enumerate(steps):
        sx0, sx1 = bounds[i], bounds[i + 1]
        cx = (sx0 + sx1) * 0.5
        cy = args.height * 0.5
        px = left + (i + 0.5) * disp_w / len(steps)
        py = top + disp_h * 0.5
        patches.append({
            "name": f"{args.ramp}-{i:02d}",
            "code10": step["code10"],
            "nits": round(step["nits"], 6),
            "image": {"x": round(cx, 3), "y": round(cy, 3)},
            "screen": {"x": round(px, 3), "y": round(py, 3)},
        })

    doc = {
        "image": args.out.name,
        "imagePath": str(args.out),
        "imageSize": {"width": args.width, "height": args.height},
        "ramp": args.ramp,
        "storage": args.storage,
        "cicp": {"primaries": gp.DEFAULT_CICP_PRIMARIES,
                 "transfer": gp.DEFAULT_CICP_TRANSFER,
                 "matrix": gp.DEFAULT_CICP_MATRIX,
                 "fullRange": gp.DEFAULT_CICP_FULL_RANGE},
        "screenSize": {"width": screen_w, "height": screen_h},
        "availableRect": {"x": ax, "y": ay, "width": aw, "height": ah},
        "displayScale": round(scale, 9),
        "patches": patches,
    }
    args.json.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"image: {args.out} ({args.width}x{args.height})")
    print(f"json : {args.json}")
    print(f"scale: {scale:.9f}  display: {disp_w:.3f}x{disp_h:.3f}  left/top: {left:.3f}/{top:.3f}")
    for p in patches:
        print(f"  {p['name']}: image=({p['image']['x']:.1f},{p['image']['y']:.1f}) "
              f"screen=({p['screen']['x']:.1f},{p['screen']['y']:.1f})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
