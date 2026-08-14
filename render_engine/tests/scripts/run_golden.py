#!/usr/bin/env python3
"""Run golden dump+compare for sandbox / depth / learn targets."""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

TARGETS = {
    "sandbox": {
        "kind": "sandbox",
        "baseline": "sandbox_gpu_headless.rgba",
        "out": "sandbox_gpu_headless.rgba",
        "dump_env": "ENGINE_GOLDEN_DUMP",
        "rmse_max": 8.0,
        "max_abs": 48,
        "frames": 3,
    },
    "sandbox_depth": {
        "kind": "sandbox",
        "baseline": "sandbox_gpu_headless_depth.rgba",
        "out": "sandbox_gpu_headless_depth.rgba",
        "dump_env": "ENGINE_GOLDEN_DUMP_DEPTH",
        "rmse_max": 2.0,
        "max_abs": 8,
        "frames": 3,
        # Also set color dump unused; depth dump is primary for this target.
        "also_color_dump": False,
    },
    "learn06": {
        "kind": "learn",
        "exe_rel": ("samples", "learn", "06_rhi_triangle"),
        "exe_name": "sample_06_rhi_triangle",
        "baseline": "learn_06_rhi_triangle.rgba",
        "out": "learn_06_rhi_triangle.rgba",
        "dump_env": "ENGINE_GOLDEN_DUMP",
        "rmse_max": 8.0,
        "max_abs": 48,
        "frames": 2,
        "args": ["--headless", "--headless_frames=2"],
    },
    "learn09": {
        "kind": "learn",
        "exe_rel": ("samples", "learn", "09_pbr_ibl"),
        "exe_name": "sample_09_pbr_ibl",
        "baseline": "learn_09_pbr_ibl.rgba",
        "out": "learn_09_pbr_ibl.rgba",
        "dump_env": "ENGINE_GOLDEN_DUMP",
        "rmse_max": 8.0,
        "max_abs": 48,
        "frames": 2,
        "args": ["--headless", "--headless_frames=2"],
    },
}


def find_exe(build_dir: Path, config: str, meta: dict) -> Path | None:
    if meta["kind"] == "sandbox":
        candidates = [
            build_dir / "samples" / "Sandbox" / config / "sample_sandbox.exe",
            build_dir / "samples" / "Sandbox" / "sample_sandbox.exe",
        ]
    else:
        rel = meta["exe_rel"]
        name = meta["exe_name"]
        candidates = [
            build_dir.joinpath(*rel) / config / f"{name}.exe",
            build_dir.joinpath(*rel) / f"{name}.exe",
        ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def run_one(root: Path, build_dir: Path, config: str, name: str, approve: bool) -> int:
    meta = TARGETS[name]
    exe = find_exe(build_dir, config, meta)
    if exe is None:
        print(f"[SKIP] {name}: executable missing under {build_dir}")
        return 0

    baseline = root / "tests" / "golden" / "baselines" / meta["baseline"]
    out = build_dir / "golden_out" / meta["out"]
    out.parent.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env[meta["dump_env"]] = str(out)
    if meta["kind"] == "sandbox" and meta["dump_env"] == "ENGINE_GOLDEN_DUMP_DEPTH":
        # Color dump optional; keep a side file so sandbox still asserts LDR.
        env["ENGINE_GOLDEN_DUMP"] = str(out.with_name("sandbox_gpu_headless_color_side.rgba"))

    if meta["kind"] == "sandbox":
        cmd = [
            str(exe),
            "--gpu-headless",
            f"--headless_frames={meta['frames']}",
            "--backend=d3d12",
        ]
    else:
        cmd = [str(exe), *meta.get("args", [])]

    print("==", name, " ".join(cmd))
    r = subprocess.run(cmd, env=env)
    if r.returncode != 0:
        print(f"[FAIL] {name} exit {r.returncode}")
        return r.returncode

    if not out.is_file():
        print(f"[SKIP] {name}: no dump written ({meta['dump_env']}) {out}")
        return 0

    if approve:
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_bytes(out.read_bytes())
        print(f"[OK] approved {name} → {baseline}")
        return 0

    if not baseline.is_file():
        print(f"[SKIP] {name}: no baseline yet — run with --approve: {baseline}")
        return 0

    compare = Path(__file__).with_name("compare_golden.py")
    return subprocess.call(
        [
            sys.executable,
            str(compare),
            "--baseline",
            str(baseline),
            "--candidate",
            str(out),
            "--rmse-max",
            str(meta["rmse_max"]),
            "--max-abs",
            str(meta["max_abs"]),
        ]
    )


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sandbox", type=Path, default=None, help="(compat) unused; use --targets")
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", type=Path, default=root / "build")
    p.add_argument(
        "--targets",
        default="sandbox,sandbox_depth,learn06,learn09",
        help="comma list: " + ",".join(TARGETS),
    )
    p.add_argument("--approve", action="store_true", help="copy candidate → baseline")
    # Legacy single-target flags kept for older CI snippets.
    p.add_argument("--baseline", type=Path, default=None)
    p.add_argument("--out", type=Path, default=None)
    p.add_argument("--frames", type=int, default=None)
    args = p.parse_args()

    names = [t.strip() for t in args.targets.split(",") if t.strip()]
    if args.baseline is not None or args.out is not None:
        # Legacy single sandbox path.
        names = ["sandbox"]
        if args.baseline:
            TARGETS["sandbox"]["baseline"] = args.baseline.name
        if args.out:
            TARGETS["sandbox"]["out"] = args.out.name
        if args.frames:
            TARGETS["sandbox"]["frames"] = args.frames

    rc = 0
    for name in names:
        if name not in TARGETS:
            print(f"[FAIL] unknown target {name}")
            return 1
        code = run_one(root, args.build_dir, args.config, name, args.approve)
        if code != 0:
            rc = code
    return rc


if __name__ == "__main__":
    sys.exit(main())
