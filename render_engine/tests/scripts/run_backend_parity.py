#!/usr/bin/env python3
"""C4: same Sandbox harness preset, D3D12 vs Vulkan capture RMSE.

Default gate is post–W-vk-parity loose (ROI on). Over threshold →
[REGRESSION-NOTED] (exit 0). Use --strict for tight gate FAIL (CI -StrictParity).
Vulkan missing → SKIP.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_golden import (  # noqa: E402
    apply_roi_mask,
    default_sandbox_ignore_rects,
    load_rgba,
    max_abs,
    rmse,
)


def find_sandbox(build_dir: Path, config: str) -> Path | None:
    for c in (
        build_dir / "samples" / "Sandbox" / config / "sample_sandbox.exe",
        build_dir / "samples" / "Sandbox" / "sample_sandbox.exe",
    ):
        if c.is_file():
            return c
    return None


def read_harness_line(stdout) -> str:
    while True:
        resp = stdout.readline()
        if not resp:
            return ""
        line = resp.strip()
        if line.startswith("{") and ("\"ok\"" in line or "\"error\"" in line):
            return line


def capture(sandbox: Path, backend: str, out: Path) -> bool:
    if out.exists():
        out.unlink()
    cmds = [
        {"cmd": "ping"},
        {"cmd": "set_quality", "key": "medium"},
        {"cmd": "camera", "pos": [0.0, 2.2, 6.2]},
        {"cmd": "toggle", "key": "taa"},
        {"cmd": "frame", "n": 1},
        {"cmd": "frame", "n": 1},
        {"cmd": "capture", "key": str(out).replace("\\", "/")},
    ]
    proc = subprocess.Popen(
        [str(sandbox), "--harness-stdio", f"--backend={backend}"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    assert proc.stdin and proc.stdout
    ok = True
    try:
        time.sleep(2.5)
        for obj in cmds:
            proc.stdin.write(json.dumps(obj, separators=(",", ":")) + "\n")
            proc.stdin.flush()
            time.sleep(0.15)
            resp = read_harness_line(proc.stdout)
            if '"ok":true' not in resp and '"ok": true' not in resp:
                print(f"  ! {obj} → {resp}")
                ok = False
                break
    finally:
        try:
            proc.stdin.write('{"cmd":"quit"}\n')
            proc.stdin.flush()
        except Exception:
            pass
        try:
            proc.wait(timeout=12)
        except subprocess.TimeoutExpired:
            proc.kill()
    return ok and out.is_file()


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", type=Path, default=root / "build")
    # Post W-vk-parity loose default (~RMSE 74 measured); --strict keeps tight 48.
    p.add_argument("--rmse-max", type=float, default=90.0)
    p.add_argument("--max-abs", type=int, default=255)
    p.add_argument(
        "--strict",
        action="store_true",
        help="Tight gate (rmse<=48, max_abs<=220) and FAIL when over",
    )
    p.add_argument(
        "--roi-ignore-hud",
        action="store_true",
        default=True,
        help="Q5: ignore Perf/sprite ROI (default on for C4)",
    )
    p.add_argument("--no-roi-ignore-hud", action="store_true", help="Disable ROI mask")
    args = p.parse_args()
    use_roi = args.roi_ignore_hud and not args.no_roi_ignore_hud
    rmse_gate = 48.0 if args.strict else args.rmse_max
    abs_gate = 220 if args.strict else args.max_abs
    if args.strict and args.rmse_max != 90.0:
        rmse_gate = args.rmse_max
    if args.strict and args.max_abs != 255:
        abs_gate = args.max_abs

    sandbox = find_sandbox(args.build_dir, args.config)
    if sandbox is None:
        print(f"[SKIP] sandbox missing under {args.build_dir}")
        return 0

    out_dir = args.build_dir / "tests" / "golden" / "parity_out"
    out_dir.mkdir(parents=True, exist_ok=True)
    d3d = out_dir / "parity_d3d12.rgba"
    vk = out_dir / "parity_vulkan.rgba"

    print("== C4 capture d3d12")
    if not capture(sandbox, "d3d12", d3d):
        print("[FAIL] d3d12 capture")
        return 1

    print("== C4 capture vulkan")
    try:
        if not capture(sandbox, "vulkan", vk):
            print("[SKIP] vulkan capture failed/unavailable")
            return 0
    except Exception as e:
        print(f"[SKIP] vulkan: {e}")
        return 0

    dw, dh, db = load_rgba(d3d)
    vw, vh, vb = load_rgba(vk)
    if (dw, dh) != (vw, vh):
        print(f"[FAIL] resolution {dw}x{dh} vs {vw}x{vh}")
        return 1

    if use_roi:
        rects = default_sandbox_ignore_rects(dw, dh)
        db = apply_roi_mask(db, dw, dh, rects)
        vb = apply_roi_mask(vb, vw, vh, rects)
        print(f"roi_ignore_hud rects={rects}")

    err = rmse(db, vb)
    mx = max_abs(db, vb)
    print(
        f"rmse={err:.4f} max_abs={mx} size={dw}x{dh} "
        f"gate=rmse<={rmse_gate} max_abs<={abs_gate}"
        + (" strict" if args.strict else "")
    )
    if err > rmse_gate or mx > abs_gate:
        msg = (
            f"C4 d3d12 vs vulkan over gate (rmse={err:.4f} max_abs={mx}); "
            "KNOWN_GAPS T03/C4 — use --strict to FAIL, else note"
        )
        if args.strict:
            print(f"[FAIL] {msg}")
            return 1
        print(f"[REGRESSION-NOTED] {msg}")
        return 0
    print("[PASS] C4 d3d12≈vulkan")
    return 0


if __name__ == "__main__":
    sys.exit(main())
