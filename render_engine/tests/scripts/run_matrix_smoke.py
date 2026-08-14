#!/usr/bin/env python3
"""Quality × toggle × backend smoke via Sandbox harness-stdio."""
from __future__ import annotations

import argparse
import json
import os
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


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--config", default="Debug")
    p.add_argument("--build-dir", type=Path, default=root / "build")
    p.add_argument("--backend", default="d3d12", choices=["d3d12", "vulkan", "both"])
    args = p.parse_args()

    sandbox = find_sandbox(args.build_dir, args.config)
    if sandbox is None:
        print(f"[SKIP] sandbox missing under {args.build_dir}")
        return 0

    backends = ["d3d12", "vulkan"] if args.backend == "both" else [args.backend]
    # Vulkan is optional sampling; failures → SKIP not FAIL for CI without VK.
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
            if '"ok":true' in resp or '"ok": true' in resp:
                ok += 1
            else:
                print(f"  ! {row['cmd']} → {resp}")
        print(f"  ok={ok}/{len(rows)}")
        if backend == "d3d12" and ok < len(rows):
            fail += 1
        if backend == "vulkan" and ok == 0:
            print("[SKIP] vulkan produced no harness responses")

    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
