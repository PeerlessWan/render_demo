#!/usr/bin/env python3
"""Quality × toggle × backend smoke via Sandbox harness-stdio.

C3: D3D12 matrix cells also capture + compare golden baselines
(default / TAA off / shadows off).
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import time
from pathlib import Path


def find_sandbox(build_dir: Path, config: str) -> Path | None:
    candidates = [
        build_dir / "samples" / "Sandbox" / config / "sample_sandbox.exe",
        build_dir / "samples" / "Sandbox" / "sample_sandbox.exe",
    ]
    for c in candidates:
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


def harness_session(sandbox: Path, backend: str, commands: list[dict]) -> list[dict]:
    cmd = [str(sandbox), "--harness-stdio", f"--backend={backend}"]
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )
    assert proc.stdin and proc.stdout
    results = []
    try:
        time.sleep(2.0)
        for obj in commands:
            line = json.dumps(obj, separators=(",", ":"))
            proc.stdin.write(line + "\n")
            proc.stdin.flush()
            time.sleep(0.12)
            resp = read_harness_line(proc.stdout)
            results.append({"cmd": obj, "resp": resp})
            if not resp:
                break
    finally:
        try:
            proc.stdin.write('{"cmd":"quit"}\n')
            proc.stdin.flush()
        except Exception:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    return results


def resp_ok(resp: str) -> bool:
    return '"ok":true' in resp or '"ok": true' in resp


def compare_rgba(root: Path, baseline: Path, candidate: Path, rmse_max: float = 12.0) -> int:
    script = root / "tests" / "scripts" / "compare_golden.py"
    r = subprocess.run(
        [
            sys.executable,
            str(script),
            "--baseline",
            str(baseline),
            "--candidate",
            str(candidate),
            "--rmse-max",
            str(rmse_max),
            "--max-abs",
            "64",
        ],
        check=False,
    )
    return r.returncode


def run_matrix_image_cells(
    root: Path,
    sandbox: Path,
    out_dir: Path,
    baseline_dir: Path,
    approve: bool,
) -> int:
    """C3: three D3D12 cells — default (High) / TAA off / shadows off."""
    out_dir.mkdir(parents=True, exist_ok=True)
    baseline_dir.mkdir(parents=True, exist_ok=True)

    cells = [
        {
            "name": "default",
            "baseline": "matrix_d3d12_default.rgba",
            "toggles_after_quality": [],
        },
        {
            "name": "taa_off",
            "baseline": "matrix_d3d12_taa_off.rgba",
            # High enables TAA; one toggle turns it off.
            "toggles_after_quality": ["taa"],
        },
        {
            "name": "shadows_off",
            "baseline": "matrix_d3d12_shadows_off.rgba",
            "toggles_after_quality": ["shadows"],
        },
    ]

    fail = 0
    for cell in cells:
        out = out_dir / cell["baseline"]
        if out.exists():
            out.unlink()
        cmds: list[dict] = [
            {"cmd": "ping"},
            {"cmd": "set_quality", "key": "high"},
            {"cmd": "camera", "pos": [0.0, 2.2, 6.2]},
        ]
        for t in cell["toggles_after_quality"]:
            cmds.append({"cmd": "toggle", "key": t})
        cmds.append({"cmd": "frame", "n": 1})
        cmds.append({"cmd": "frame", "n": 1})
        cmds.append({"cmd": "capture", "key": str(out).replace("\\", "/")})

        print(f"== matrix-img cell={cell['name']} capture={out.name}")
        try:
            rows = harness_session(sandbox, "d3d12", cmds)
        except Exception as e:
            print(f"[FAIL] matrix-img {cell['name']}: {e}")
            fail += 1
            continue

        ok = sum(1 for r in rows if resp_ok(r["resp"]))
        print(f"  harness ok={ok}/{len(rows)}")
        if ok < len(rows) or not out.is_file():
            for r in rows:
                if not resp_ok(r["resp"]):
                    print(f"  ! {r['cmd']} → {r['resp']}")
            print(f"[FAIL] matrix-img {cell['name']}: capture missing or harness error")
            fail += 1
            continue

        baseline = baseline_dir / cell["baseline"]
        if approve:
            shutil.copyfile(out, baseline)
            print(f"[OK] approved {cell['name']} → {baseline.name}")
            continue

        if not baseline.is_file():
            print(f"[SKIP] {cell['name']}: no baseline — re-run with --approve")
            continue

        code = compare_rgba(root, baseline, out)
        if code != 0:
            print(f"[FAIL] matrix-img {cell['name']} golden mismatch")
            fail += 1
        else:
            print(f"[PASS] matrix-img {cell['name']}")

    return fail


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", type=Path, default=root / "build")
    p.add_argument("--backend", default="d3d12", choices=["d3d12", "vulkan", "both"])
    p.add_argument(
        "--approve",
        action="store_true",
        help="copy C3 matrix candidates → baselines",
    )
    p.add_argument(
        "--skip-image",
        action="store_true",
        help="only harness toggle smoke (no C3 capture)",
    )
    args = p.parse_args()

    sandbox = find_sandbox(args.build_dir, args.config)
    if sandbox is None:
        print(f"[SKIP] sandbox missing under {args.build_dir}")
        return 0

    backends = ["d3d12", "vulkan"] if args.backend == "both" else [args.backend]
    toggles = ["taa", "ssao", "ibl", "shadows"]
    qualities = ["low", "medium", "high"]
    fail = 0

    for backend in backends:
        cmds: list[dict] = [{"cmd": "ping"}]
        for q in qualities:
            cmds.append({"cmd": "set_quality", "key": q})
            for t in toggles:
                cmds.append({"cmd": "toggle", "key": t})
            cmds.append({"cmd": "profiler_snapshot"})
            cmds.append({"cmd": "frame", "n": 1})
        cmds.append({"cmd": "query_features"})

        print(f"== matrix backend={backend} cmds={len(cmds)}")
        try:
            rows = harness_session(sandbox, backend, cmds)
        except Exception as e:
            if backend == "vulkan":
                print(f"[SKIP] vulkan matrix: {e}")
                continue
            print(f"[FAIL] {backend}: {e}")
            fail += 1
            continue

        ok = 0
        for row in rows:
            resp = row["resp"]
            if resp_ok(resp):
                ok += 1
            else:
                print(f"  ! {row['cmd']} → {resp}")
        print(f"  ok={ok}/{len(rows)}")
        if backend == "d3d12" and ok < len(rows):
            fail += 1
        if backend == "vulkan" and ok == 0:
            print("[SKIP] vulkan produced no harness responses")

    if not args.skip_image and args.backend in ("d3d12", "both"):
        out_dir = args.build_dir / "tests" / "golden" / "matrix_out"
        baseline_dir = root / "tests" / "golden" / "baselines"
        fail += run_matrix_image_cells(root, sandbox, out_dir, baseline_dir, args.approve)

    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
