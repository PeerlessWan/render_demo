#!/usr/bin/env python3
"""C6: hash compiled DXIL/SPIR-V shaders; fail on unintentional bytecode drift.

Manifest: tests/golden/shader_hashes.json
Approve: python check_shader_hashes.py --approve
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

# Relative to Sandbox shaders out dir (shared Debug/Release).
WATCH = [
    "lit_cube.vs.cso",
    "lit_cube.ps.cso",
    "shadow.vs.cso",
    "shadow.ps.cso",
    "post_ssao_taa.vs.cso",
    "post_ssao_taa.ps.cso",
    "instance_cull_cs.cso",
    "skybox.vs.cso",
    "skybox.ps.cso",
    "lit_cube_vk.vs.spv",
    "lit_cube_vk.ps.spv",
]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def find_shader_dir(build_dir: Path) -> Path | None:
    candidates = [
        build_dir / "samples" / "Sandbox" / "shaders",
        build_dir / "shaders",
    ]
    for c in candidates:
        if c.is_dir():
            return c
    return None


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir", type=Path, default=root / "build")
    p.add_argument("--approve", action="store_true", help="rewrite manifest from current binaries")
    p.add_argument(
        "--manifest",
        type=Path,
        default=root / "tests" / "golden" / "shader_hashes.json",
    )
    args = p.parse_args()

    shader_dir = find_shader_dir(args.build_dir)
    if shader_dir is None:
        print(f"[SKIP] shader dir missing under {args.build_dir}")
        return 0

    current: dict[str, str] = {}
    missing: list[str] = []
    for name in WATCH:
        path = shader_dir / name
        if not path.is_file():
            missing.append(name)
            continue
        current[name] = sha256_file(path)

    if not current:
        print("[SKIP] no watched shaders present")
        return 0

    if missing:
        print(f"[WARN] missing (ignored until built): {', '.join(missing)}")

    if args.approve:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "version": 1,
            "note": "C6 bytecode hash; update with --approve after intentional shader rebuild",
            "files": current,
        }
        args.manifest.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"[OK] approved {len(current)} hashes → {args.manifest}")
        return 0

    if not args.manifest.is_file():
        print(f"[SKIP] no manifest yet — run with --approve: {args.manifest}")
        return 0

    data = json.loads(args.manifest.read_text(encoding="utf-8"))
    expected = data.get("files") or {}
    fail = 0
    for name, digest in current.items():
        if name not in expected:
            print(f"[FAIL] {name}: not in manifest (re-run --approve?)")
            fail += 1
            continue
        if digest != expected[name]:
            print(f"[FAIL] {name}: hash mismatch")
            print(f"  expected {expected[name]}")
            print(f"  got      {digest}")
            fail += 1
        else:
            print(f"[PASS] {name}")

    for name in expected:
        if name not in current and name not in missing:
            print(f"[FAIL] {name}: in manifest but missing on disk")
            fail += 1

    if fail:
        print(f"[FAIL] shader hash gate ({fail})")
        return 1
    print(f"[PASS] shader hashes ok={len(current)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
