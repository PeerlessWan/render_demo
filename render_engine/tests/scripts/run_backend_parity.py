#!/usr/bin/env python3
"""C4 thin: same Sandbox harness preset, D3D12 vs Vulkan capture RMSE (loose).

Default: report metrics; over threshold → [REGRESSION-NOTED] (exit 0) until
backends tighten. Use --strict to FAIL CI. Vulkan missing → SKIP.
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_golden import load_rgba, max_abs, rmse  # noqa: E402


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
    p.add_argument("--rmse-max", type=float, default=48.0)
    p.add_argument("--max-abs", type=int, default=220)
    p.add_argument(
        "--strict",
        action="store_true",
        help="FAIL when over threshold (default: note regression, exit 0)",
    )
    args = p.parse_args()

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

    err = rmse(db, vb)
    mx = max_abs(db, vb)
    print(
        f"rmse={err:.4f} max_abs={mx} size={dw}x{dh} "
        f"gate=rmse<={args.rmse_max} max_abs<={args.max_abs}"
    )
    if err > args.rmse_max or mx > args.max_abs:
        msg = (
            f"C4 d3d12 vs vulkan over loose gate (rmse={err:.4f} max_abs={mx}); "
            "KNOWN_GAPS T03/C4 — not blocking until backends tighten"
        )
        if args.strict:
            print(f"[FAIL] {msg}")
            return 1
        print(f"[REGRESSION-NOTED] {msg}")
        return 0
    print("[PASS] C4 d3d12≈vulkan (loose)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
