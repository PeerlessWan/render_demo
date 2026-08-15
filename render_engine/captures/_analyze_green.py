#!/usr/bin/env python3
"""Analyze Sandbox BMP dumps; optional green-pillar mask MAD for CSM checks.

Usage:
  python captures/_analyze_green.py [dump_dir]
Default dump: sandbox_20260815_170746 (pillar shimmer baseline).
"""
from __future__ import annotations

import pathlib
import statistics as stats
import struct
import sys

root = pathlib.Path(
    sys.argv[1]
    if len(sys.argv) > 1
    else pathlib.Path(__file__).resolve().parent / "sandbox_20260815_170746"
)
files = sorted(root.glob("frame_*.bmp"))
print("dir", root)
print("frames", len(files))
if not files:
    raise SystemExit(1)


def load(path: pathlib.Path):
    b = path.read_bytes()
    off = struct.unpack_from("<I", b, 10)[0]
    w, h = struct.unpack_from("<ii", b, 18)
    stride = (w * 3 + 3) & ~3
    step = 2
    samples = []
    for y in range(0, h, step):
        src_y = h - 1 - y
        row = off + src_y * stride
        for x in range(0, w, step):
            i = row + x * 3
            B, G, R = b[i], b[i + 1], b[i + 2]
            lum = 0.2126 * R + 0.7152 * G + 0.0722 * B
            samples.append((lum, R, G, B))
    return samples


def is_green(R, G, B):
    return G > R + 18 and G > B + 18 and G > 40


prev = None
gmad = []
glum = []
for fi, f in enumerate(files):
    samples = load(f)
    if prev is not None:
        acc = 0.0
        n = 0
        for (lum, R, G, B), (lum2, R2, G2, B2) in zip(samples, prev):
            if is_green(R, G, B) or is_green(R2, G2, B2):
                acc += abs(lum - lum2)
                n += 1
        gmad.append(acc / max(n, 1))
        g_now = [s[0] for s in samples if is_green(s[1], s[2], s[3])]
        g_prev = [s[0] for s in prev if is_green(s[1], s[2], s[3])]
        if g_now and g_prev:
            glum.append(sum(g_now) / len(g_now) - sum(g_prev) / len(g_prev))
        else:
            glum.append(0.0)
    prev = samples

print(
    "green mad median/avg/max",
    round(stats.median(gmad), 3),
    round(stats.mean(gmad), 3),
    round(max(gmad), 3),
)
print("|dGLum|>0.8", sum(1 for d in glum if abs(d) > 0.8))
pops = []
for i in range(len(glum) - 1):
    if abs(glum[i]) > 0.8 and abs(glum[i + 1]) > 0.8 and glum[i] * glum[i + 1] < 0:
        pops.append((i, glum[i], glum[i + 1]))
print("paired opposite |dGLum|>0.8", len(pops))
for p in pops[:10]:
    print(f"  pair {p[0]}:{p[1]:+.2f} then {p[0]+1}:{p[2]:+.2f}")
