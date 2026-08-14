#!/usr/bin/env python3
"""Run Sandbox gpu-headless, dump last-frame .rgba, compare to baseline."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sandbox", type=Path, default=None)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", type=Path, default=root / "build")
    p.add_argument(
        "--baseline",
        type=Path,
        default=root / "tests" / "golden" / "baselines" / "sandbox_gpu_headless.rgba",
    )
    p.add_argument(
        "--out",
        type=Path,
        default=root / "build" / "golden_out" / "sandbox_gpu_headless.rgba",
    )
    p.add_argument("--frames", type=int, default=3)
    p.add_argument("--approve", action="store_true", help="copy candidate → baseline")
    args = p.parse_args()

    sandbox = args.sandbox
    if sandbox is None:
        sandbox = (
            args.build_dir / "samples" / "Sandbox" / args.config / "sample_sandbox.exe"
        )
        if not sandbox.is_file():
            sandbox = args.build_dir / "samples" / "Sandbox" / "sample_sandbox.exe"

    if not sandbox.is_file():
        print(f"[SKIP] sandbox missing: {sandbox}")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["ENGINE_GOLDEN_DUMP"] = str(args.out)

    cmd = [
        str(sandbox),
        "--gpu-headless",
        f"--headless_frames={args.frames}",
        "--backend=d3d12",
    ]
    print("==", " ".join(cmd))
    r = subprocess.run(cmd, env=env)
    if r.returncode != 0:
        print(f"[FAIL] sandbox exit {r.returncode}")
        return r.returncode

    if not args.out.is_file():
        print(f"[SKIP] no dump written (ENGINE_GOLDEN_DUMP ignored?) {args.out}")
        return 0

    if args.approve:
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_bytes(args.out.read_bytes())
        print(f"[OK] approved → {args.baseline}")
        return 0

    if not args.baseline.is_file():
        print(f"[SKIP] no baseline yet — run with --approve once: {args.baseline}")
        return 0

    compare = Path(__file__).with_name("compare_golden.py")
    return subprocess.call(
        [
            sys.executable,
            str(compare),
            "--baseline",
            str(args.baseline),
            "--candidate",
            str(args.out),
        ]
    )


if __name__ == "__main__":
    sys.exit(main())
