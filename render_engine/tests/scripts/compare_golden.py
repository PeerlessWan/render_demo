#!/usr/bin/env python3
"""Compare a readback RGBA/PNG against a golden baseline (RMSE / max abs)."""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path


def load_rgba(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if path.suffix.lower() == ".rgba":
        # header: u32 w, u32 h, then w*h*4 bytes
        if len(data) < 8:
            raise SystemExit(f"invalid rgba: {path}")
        w, h = struct.unpack_from("<II", data, 0)
        need = 8 + w * h * 4
        if len(data) < need:
            raise SystemExit(f"truncated rgba: {path}")
        return w, h, data[8:need]
    if path.suffix.lower() == ".png":
        try:
            from PIL import Image  # type: ignore
        except ImportError as e:
            raise SystemExit("PNG compare needs Pillow, or use .rgba baselines") from e
        im = Image.open(path).convert("RGBA")
        return im.width, im.height, im.tobytes()
    raise SystemExit(f"unsupported format: {path}")


def rmse(a: bytes, b: bytes) -> float:
    if len(a) != len(b):
        raise SystemExit(f"size mismatch {len(a)} vs {len(b)}")
    acc = 0.0
    n = len(a)
    for i in range(n):
        d = a[i] - b[i]
        acc += d * d
    return math.sqrt(acc / max(n, 1))


def max_abs(a: bytes, b: bytes) -> int:
    m = 0
    for i in range(len(a)):
        d = abs(a[i] - b[i])
        if d > m:
            m = d
    return m


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--baseline", required=True, type=Path)
    p.add_argument("--candidate", required=True, type=Path)
    p.add_argument("--rmse-max", type=float, default=8.0, help="max RMSE over bytes [0,255]")
    p.add_argument("--max-abs", type=int, default=48, help="max per-channel abs diff")
    args = p.parse_args()

    if not args.baseline.is_file():
        print(f"[SKIP] missing baseline {args.baseline}")
        return 0
    if not args.candidate.is_file():
        print(f"[FAIL] missing candidate {args.candidate}")
        return 1

    bw, bh, b = load_rgba(args.baseline)
    cw, ch, c = load_rgba(args.candidate)
    if (bw, bh) != (cw, ch):
        print(f"[FAIL] resolution {bw}x{bh} vs {cw}x{ch}")
        return 1

    err = rmse(b, c)
    mx = max_abs(b, c)
    print(f"rmse={err:.4f} max_abs={mx} size={bw}x{bh}")
    if err > args.rmse_max or mx > args.max_abs:
        print("[FAIL] golden mismatch")
        return 1
    print("[PASS] golden")
    return 0


if __name__ == "__main__":
    sys.exit(main())
