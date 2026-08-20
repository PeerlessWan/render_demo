#!/usr/bin/env python3
"""Split oversized d3d12/vulkan device_core / device_post into smaller TUs."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = '#include "{internal}"\n\nnamespace engine::rhi {{\n\n'
FOOTER = "\n}  // namespace engine::rhi\n"

FN_START = re.compile(
    r"^(?:(?:Status|void|bool|float|int|UINT64|std::uint32_t)\s+)?"
    r"(?:D3D12Device|VulkanDevice)::(~?\w+)\s*\("
)


def parse_functions(text: str) -> list[tuple[str, int, int]]:
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
    lines = text.splitlines(keepends=True)
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
    # --- D3D12 core → core + swapchain + readback ---
    split_one(
        "engine/backends/d3d12/d3d12_device_core.cpp",
        "d3d12_device_internal.h",
        {
            "core": {
                "Init",
                "~D3D12Device",
                "SetVSync",
                "width",
                "height",
                "BeginFrame",
                "Clear",
                "DrawSimpleMesh",
                "SetSubmitConfig",
                "Present",
                "Resize",
                "SetupSimpleMesh",
                "SetDrawViewport",
                "SetPreferLdrTarget",
                "GpuPassBegin",
                "GpuPassEnd",
                "ObjectCbOffset",
                "Transition",
                "WaitGpuSubmitted",
                "WaitGpu",
            },
            "swapchain": {
                "CreateSwapchain",
                "CreateOffscreenBackbuffers",
                "TryEnableDisplayHdr",
                "CreateFrameResources",
                "CreateRenderTargets",
                "CreateDepthBuffer",
                "CreateVertexBuffer",
                "CreateCheckerTexture",
            },
            "readback": {
                "ReadbackTextureStub",
                "ReadbackDepthRgbaStub",
                "EnsureColorReadbackBuffer",
                "EnsureDepthReadbackBuffer",
                "CreateGpuTimestampResources",
                "ReadbackGpuPassTimings",
            },
        },
        {
            "core": "d3d12_device_core.cpp",
            "swapchain": "d3d12_device_swapchain.cpp",
            "readback": "d3d12_device_readback.cpp",
        },
    )

    # --- D3D12 post → post + overlay (ui/quad/debug) ---
    split_one(
        "engine/backends/d3d12/d3d12_device_post.cpp",
        "d3d12_device_internal.h",
        {
            "post": {
                "SetupPostMesh",
                "ResolvePostEffects",
                "PostCbOffset",
                "CreatePostColorTargets",
                "UpdatePostSrvs",
            },
            "overlay": {
                "DrawScreenQuads",
                "DrawDebugLines",
                "SetupUiMesh",
                "UploadUiFontAtlas",
                "DrawUiMesh",
                "SetupScreenQuads",
                "SetupDebugLines",
            },
        },
        {
            "post": "d3d12_device_post.cpp",
            "overlay": "d3d12_device_overlay.cpp",
        },
    )

    # --- Vulkan core → core + swapchain + targets ---
    split_one(
        "engine/backends/vulkan/vulkan_device_core.cpp",
        "vulkan_device_internal.h",
        {
            "core": {
                "Init",
                "~VulkanDevice",
                "width",
                "height",
                "SetVSync",
                "BeginFrame",
                "Clear",
                "Present",
                "Resize",
                "DrawSimpleMesh",
                "SetupSimpleMesh",
                "ReadbackTextureStub",
                "DestroyTex2D",
                "DestroyPrefilterCube",
                "BeginPresentRenderPass",
            },
            "swapchain": {
                "CreateInstance",
                "CreateSurface",
                "PickPhysicalDevice",
                "CreateLogicalDevice",
                "CreateSwapchain",
                "DestroySwapchainViews",
                "DestroySwapchain",
                "RecreateSwapchain",
            },
            "targets": {
                "CreateImage",
                "CreateRenderPass",
                "DestroyPresentRenderPasses",
                "CreatePresentRenderPass",
                "DestroyDepthOnly",
                "CreateDepthResources",
                "CreateShaderModule",
                "BarrierDepth",
                "BarrierHistory",
                "CopySwapchainToHistory",
                "DestroyHistoryOnly",
                "EnsureHistory",
                "DestroySceneColorOnly",
                "EnsureSceneColor",
                "CaptureSceneColorIntermediate",
            },
        },
        {
            "core": "vulkan_device_core.cpp",
            "swapchain": "vulkan_device_swapchain.cpp",
            "targets": "vulkan_device_targets.cpp",
        },
    )

    # --- Vulkan post → post + overlay ---
    split_one(
        "engine/backends/vulkan/vulkan_device_post.cpp",
        "vulkan_device_internal.h",
        {
            "post": {
                "SetupPostMesh",
                "ResolvePostEffects",
                "CreatePostColorRenderPass",
                "DestroyPostFramebuffersOnly",
                "CreatePostFramebuffers",
                "EnsurePostUb",
                "UploadPostCB",
                "CreatePostPipeline",
                "EnsurePostDescriptors",
                "UpdatePostDescriptors",
                "DestroyPostResources",
            },
            "overlay": {
                "DrawScreenQuads",
                "DrawDebugLines",
                "SetupUiMesh",
                "UploadUiFontAtlas",
                "DrawUiMesh",
                "DestroyUiResources",
                "DestroyQuadResources",
                "DestroyDebugResources",
                "SetupScreenQuads",
                "SetupDebugLines",
            },
        },
        {
            "post": "vulkan_device_post.cpp",
            "overlay": "vulkan_device_overlay.cpp",
        },
    )


if __name__ == "__main__":
    main()
