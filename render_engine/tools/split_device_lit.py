#!/usr/bin/env python3
"""Split oversized d3d12/vulkan_device_lit.cpp into lit / shadow / env TUs."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = '#include "{internal}"\n\nnamespace engine::rhi {{\n\n'
FOOTER = "\n}  // namespace engine::rhi\n"

FN_START = re.compile(
    r"^(?:Status|void|bool|float|int|UINT64|std::uint32_t)\s+"
    r"(?:D3D12Device|VulkanDevice)::(\w+)\s*\("
)  # also matches trailing `) const {`


def parse_functions(text: str) -> list[tuple[str, int, int]]:
    """Return (name, start_line0, end_line0_exclusive) for each method body."""
    lines = text.splitlines(keepends=True)
    starts: list[tuple[str, int]] = []
    for i, line in enumerate(lines):
        m = FN_START.match(line)
        if m:
            starts.append((m.group(1), i))
    content_end = len(lines)
    while content_end > 0:
        s = lines[content_end - 1].strip()
        if s == "" or (s.startswith("}") and "namespace" in s):
            content_end -= 1
            continue
        break
    out: list[tuple[str, int, int]] = []
    for idx, (name, start) in enumerate(starts):
        end = starts[idx + 1][1] if idx + 1 < len(starts) else content_end
        while end > start and lines[end - 1].strip() == "":
            end -= 1
        out.append((name, start, end))
    return out


def emit(path: Path, internal: str, chunks: list[str]) -> None:
    body = "".join(chunks).rstrip() + "\n"
    path.write_text(HEADER.format(internal=internal) + body + FOOTER, encoding="utf-8", newline="\n")
    print(f"wrote {path.relative_to(ROOT)} ({path.read_text(encoding='utf-8').count(chr(10))} lines)")


def split_one(
    src_rel: str,
    internal: str,
    buckets: dict[str, set[str]],
    out_names: dict[str, str],
) -> None:
    src = ROOT / src_rel
    text = src.read_text(encoding="utf-8")
    # Strip existing include + namespace wrapper for re-emit.
    lines = text.splitlines(keepends=True)
    # Find first method; keep only method bodies.
    funcs = parse_functions(text)
    if not funcs:
        raise SystemExit(f"no methods in {src}")

    bucket_chunks: dict[str, list[str]] = {k: [] for k in buckets}
    assigned: set[str] = set()
    for name, start, end in funcs:
        chunk = "".join(lines[start:end])
        if not chunk.endswith("\n"):
            chunk += "\n"
        chunk += "\n"
        placed = False
        for bucket, names in buckets.items():
            if name in names:
                bucket_chunks[bucket].append(chunk)
                assigned.add(name)
                placed = True
                break
        if not placed:
            raise SystemExit(f"{src.name}: unassigned method {name}")

    missing = set().union(*buckets.values()) - assigned
    if missing:
        raise SystemExit(f"{src.name}: missing methods {sorted(missing)}")

    for bucket, chunks in bucket_chunks.items():
        out = src.parent / out_names[bucket]
        emit(out, internal, chunks)


def main() -> None:
    d3d12_buckets = {
        "lit": {
            "SetupLitMesh",
            "SetFrameLighting",
            "BindSceneColorTargets",
            "DrawLitCube",
            "DrawLitCubes",
            "DrawTransparentLitCubes",
            "DrawLitCubesWithPso",
            "UploadInstanceTransforms",
            "DrawLitInstanced",
            "FrameCbOffset",
            "CreateLitAlbedoTexture",
            "CreateLitOrmTexture",
            "CreateLitAlbedoTextureSlot1",
            "CreateLitOrmTextureSlot1",
            "UploadLitAlbedoRgba",
            "UploadLitOrmRgba",
            "CreateCubeMesh",
            "UploadLitGeometry",
            "CreateLitConstantBuffers",
        },
        "shadow": {
            "BeginShadowPass",
            "BindShadowCascade",
            "DrawShadowCubes",
            "EndShadowPass",
            "BeginLocalShadowPass",
            "BindLocalShadowTile",
            "EndLocalShadowPass",
            "ShadowVpCbOffset",
            "CreateShadowMap",
            "CreateLocalShadowMap",
        },
        "env": {
            "EnsureProbeFaceTargets",
            "CaptureReflectionProbeGpu",
            "SkyCbOffset",
            "BindReflectionCubeSrv",
            "EnsureDefaultReflectionCubemap",
            "UploadReflectionCubemap",
            "BindCubeSrv",
            "UploadIblIrradianceCubemap",
            "UploadIblPrefilterCubemap",
            "UploadIblBrdfLut",
            "EnsureDefaultProbeGiAndSoftShadowTextures",
            "UploadProbeIrradianceAtlas",
            "UploadSoftShadowMask",
            "SetupSkybox",
            "UploadSkyCubemap",
            "DrawSkybox",
            "UploadCubemapResource",
        },
    }
    split_one(
        "engine/backends/d3d12/d3d12_device_lit.cpp",
        "d3d12_device_internal.h",
        d3d12_buckets,
        {
            "lit": "d3d12_device_lit.cpp",
            "shadow": "d3d12_device_shadow.cpp",
            "env": "d3d12_device_env.cpp",
        },
    )

    vk_buckets = {
        "lit": {
            "SetupLitMesh",
            "SetFrameLighting",
            "UploadInstanceTransforms",
            "DrawLitInstanced",
            "DrawLitCube",
            "DrawTransparentLitCubes",
            "DrawLitCubes",
            "DrawLitCubesWithPipeline",
            "UploadLitAlbedoRgba",
            "UploadLitOrmRgba",
            "UploadLitGeometry",
            "DestroyMeshSlot",
            "UpdateLitCombinedBinding",  # may appear twice — see below
            "UpdateLitInstanceBinding",
            "BeginLitRenderPass",
            "CreateFrameSync",
            "CreateLitRenderPass",
            "CreateLitPipeline",
            "CreateCubeMesh",
            "DestroyLitResources",
        },
        "shadow": {
            "BeginShadowPass",
            "BindShadowCascade",
            "DrawShadowCubes",
            "EndShadowPass",
            "BeginLocalShadowPass",
            "BindLocalShadowTile",
            "EndLocalShadowPass",
            "BarrierShadowImage",
            "BarrierLocalShadowImage",
            "ImmediateTransitionShadow",
            "CreateShadowResources",
        },
        "env": {
            "UploadReflectionCubemap",
            "UploadIblIrradianceCubemap",
            "UploadIblPrefilterCubemap",
            "UploadIblBrdfLut",
            "UploadProbeIrradianceAtlas",
            "UploadSoftShadowMask",
            "SetupSkybox",
            "UploadSkyCubemap",
            "DrawSkybox",
            "DestroyIblCube",
            "DestroyReflectionProbeCube",
            "DestroySkyCube",
            "DestroySkyResources",
        },
    }
    # UpdateLitCombinedBinding is overloaded (2 defs); both go to lit via name match.
    split_one(
        "engine/backends/vulkan/vulkan_device_lit.cpp",
        "vulkan_device_internal.h",
        vk_buckets,
        {
            "lit": "vulkan_device_lit.cpp",
            "shadow": "vulkan_device_shadow.cpp",
            "env": "vulkan_device_env.cpp",
        },
    )


if __name__ == "__main__":
    main()
