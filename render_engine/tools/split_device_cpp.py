#!/usr/bin/env python3
"""Convert monolithic inline-class *device.cpp into header + domain .cpp TUs."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

CORE = {
    "Init", "is_headless", "api_kind", "SetVSync", "vsync", "CurrentBbIndex",
    "width", "height", "BeginFrame", "Clear", "Present", "Resize",
    "DrawSimpleMesh", "SetupSimpleMesh", "SetSubmitConfig",
    "ReadbackTextureStub", "ReadbackDepthRgbaStub",
    "GpuPassBegin", "GpuPassEnd", "GpuTimestampAvailable", "LastGpuPassTimings",
    "SetDrawViewport", "SetPreferLdrTarget",
    "CreateSwapchain", "CreateOffscreenBackbuffers", "TryEnableDisplayHdr",
    "CreateFrameResources", "CreateRenderTargets", "CreateDepthBuffer",
    "CreateVertexBuffer", "CreateCheckerTexture", "Transition",
    "WaitGpuSubmitted", "WaitGpu", "EnsureColorReadbackBuffer",
    "EnsureDepthReadbackBuffer", "CreateGpuTimestampResources",
    "ReadbackGpuPassTimings",
}
COMPUTE = {
    "DispatchCompute", "SetupInstanceCullCompute", "DispatchInstanceCull",
    "SetupLightTileCullCompute", "DispatchLightTileCull",
    "TryDispatchLightTileCullGpu", "ProbeBindlessMinimalPath",
    "UploadIndirectIndexedArgs", "ExecuteIndirectIndexed",
    "BeginOneShot", "EndOneShot",
}
LIT = {
    "SetupLitMesh", "SetFrameLighting", "BeginShadowPass", "BindShadowCascade",
    "DrawShadowCubes", "BindSceneColorTargets", "EndShadowPass",
    "BeginLocalShadowPass", "BindLocalShadowTile", "EndLocalShadowPass",
    "DrawLitCube", "DrawLitCubes", "DrawTransparentLitCubes",
    "DrawLitCubesWithPso", "UploadInstanceTransforms", "DrawLitInstanced",
    "EnsureProbeFaceTargets", "CaptureReflectionProbeGpu",
    "CreateShadowMap", "CreateLocalShadowMap",
    "CreateLitAlbedoTexture", "CreateLitOrmTexture",
    "CreateLitAlbedoTextureSlot1", "CreateLitOrmTextureSlot1",
    "BindReflectionCubeSrv", "EnsureDefaultReflectionCubemap",
    "UploadReflectionCubemap", "BindCubeSrv",
    "UploadIblIrradianceCubemap", "UploadIblPrefilterCubemap", "UploadIblBrdfLut",
    "SetupSkybox", "UploadSkyCubemap", "DrawSkybox", "UploadCubemapResource",
    "UploadLitAlbedoRgba", "UploadLitOrmRgba", "CreateCubeMesh",
    "UploadLitGeometry", "CreateLitConstantBuffers",
}
POST = {
    "SetupPostMesh", "ResolvePostEffects", "DrawScreenQuads", "DrawDebugLines",
    "SetupUiMesh", "UploadUiFontAtlas", "DrawUiMesh",
    "CreatePostColorTargets", "UpdatePostSrvs",
    "SetupScreenQuads", "SetupDebugLines",
}
RESOURCES = {"UploadRgbaTexture"}


def find_brace(text: str, open_idx: int) -> int:
    depth = 0
    i = open_idx
    n = len(text)
    in_str = in_char = in_line = in_block = False
    while i < n:
        c, nxt = text[i], text[i + 1] if i + 1 < n else ""
        if in_line:
            if c == "\n":
                in_line = False
            i += 1
            continue
        if in_block:
            if c == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_str:
            if c == "\\" and nxt:
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\" and nxt:
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == "/" and nxt == "/":
            in_line = True
            i += 2
            continue
        if c == "/" and nxt == "*":
            in_block = True
            i += 2
            continue
        if c == '"':
            in_str = True
            i += 1
            continue
        if c == "'":
            in_char = True
            i += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise RuntimeError("unbalanced")


def domain_for(name: str) -> str:
    n = name.lstrip("~")
    if name.startswith("~") or n in CORE:
        return "core"
    if n in COMPUTE:
        return "compute"
    if n in LIT:
        return "lit"
    if n in POST:
        return "post"
    if n in RESOURCES:
        return "resources"
    low = n.lower()
    if any(k in low for k in ("cull", "compute", "indirect", "tile", "bindless", "oneshot")):
        return "compute"
    if any(k in low for k in ("post", "ui", "quad", "debug", "font")):
        return "post"
    if any(k in low for k in ("upload", "texture", "srv", "buffer", "descriptor", "rgba")):
        return "resources"
    if any(
        k in low
        for k in ("lit", "shadow", "sky", "ibl", "probe", "draw", "mesh", "albedo", "orm", "frame", "cascade")
    ):
        return "lit"
    if any(k in low for k in ("create", "wait", "transition", "readback", "swapchain", "fence")):
        return "core"
    return "core"


def extract_methods(body: str) -> list[tuple[str, str, str, int, int]]:
    """(name, signature_without_brace, full_text, start, end)."""
    methods = []
    for m in re.finditer(
        r"^  ((?:\[\[nodiscard\]\] )?(?:Status|void|bool|int|float|double|UINT|UINT64|"
        r"DeviceApiKind|HRESULT|std::vector<GpuPassTiming>))\s+(~?\w+)\s*\(",
        body,
        re.M,
    ):
        name = m.group(2)
        i = m.end() - 1
        depth = 0
        while i < len(body):
            if body[i] == "(":
                depth += 1
            elif body[i] == ")":
                depth -= 1
                if depth == 0:
                    i += 1
                    break
            i += 1
        while i < len(body) and body[i] in " \t\r\n":
            i += 1
        while True:
            if body.startswith("const", i):
                i += 5
            elif body.startswith("override", i):
                i += 8
            elif body.startswith("final", i):
                i += 5
            else:
                break
            while i < len(body) and body[i] in " \t\r\n":
                i += 1
        if i >= len(body) or body[i] != "{":
            continue
        end = find_brace(body, i) + 1
        sig = body[m.start() : i].rstrip()
        full = body[m.start() : end]
        methods.append((name, sig, full, m.start(), end))
    return methods


def to_outofline(full: str, class_name: str) -> str:
    lines = full.splitlines()
    first = lines[0][2:] if lines[0].startswith("  ") else lines[0]
    # Insert Class:: before method name (identifier before '(')
    paren = first.find("(")
    # walk back over name
    j = paren - 1
    while j >= 0 and first[j] in " \t":
        j -= 1
    end_name = j + 1
    while j >= 0 and (first[j].isalnum() or first[j] in "_~"):
        j -= 1
    start_name = j + 1
    name = first[start_name:end_name]
    first = first[:start_name] + f"{class_name}::" + first[start_name:]
    out = [first]
    for line in lines[1:]:
        out.append(line[2:] if line.startswith("  ") else line)
    return "\n".join(out)


def split_kind(kind: str) -> None:
    if kind == "d3d12":
        src = ROOT / "engine/backends/d3d12/d3d12_device.cpp"
        class_name = "D3D12Device"
        prefix = "d3d12_device"
    else:
        src = ROOT / "engine/backends/vulkan/vulkan_device.cpp"
        class_name = "VulkanDevice"
        prefix = "vulkan_device"
    out_dir = src.parent

    bak = src.with_suffix(".cpp.w20bak")
    text = src.read_text(encoding="utf-8")
    # Prefer backup if already split once
    if bak.exists() and "internal.h" in text:
        text = bak.read_text(encoding="utf-8")
        print("restored from backup for re-split")
    elif not bak.exists():
        bak.write_text(text, encoding="utf-8")
        print(f"backup -> {bak.name}")

    m = re.search(rf"class {class_name} final : public IDevice \{{", text)
    if not m:
        raise SystemExit("class not found")
    class_open = m.end() - 1
    class_close = find_brace(text, class_open)
    preamble = text[: m.start()]
    class_body = text[class_open + 1 : class_close]
    epilogue = text[class_close + 1 :]

    priv_m = re.search(r"\n private:", class_body)
    if not priv_m:
        raise SystemExit("private: not found")
    public_body = class_body[: priv_m.start()]
    private_body = class_body[priv_m.start() + 1 :]

    pub = extract_methods(public_body)
    priv = extract_methods(private_body)
    print(f"{kind}: public={len(pub)} private={len(priv)}")

    # Strip private methods -> declarations; keep fields
    priv_stripped = private_body
    for name, sig, full, s, e in sorted(priv, key=lambda x: -x[3]):
        priv_stripped = priv_stripped[:s] + sig.rstrip() + ";\n" + priv_stripped[e:]

    pub_decls = [sig.rstrip() + ";" for name, sig, full, s, e in pub]

    buckets: dict[str, list[str]] = {
        "core": [],
        "compute": [],
        "lit": [],
        "post": [],
        "resources": [],
    }
    for name, sig, full, s, e in pub + priv:
        buckets[domain_for(name)].append(to_outofline(full, class_name) + "\n")

    # Header
    ns = re.search(r"namespace engine::rhi \{", preamble)
    includes = preamble[: ns.start()] if ns else ""
    anon = preamble[ns.end() :] if ns else preamble
    # Promote anonymous namespace helpers to inline detail namespace for ODR-safe header use
    anon2 = anon.replace("namespace {\n", "namespace device_detail {\n", 1)
    if anon2.rstrip().endswith("}  // namespace"):
        pass
    # close detail instead of anon
    anon2 = re.sub(
        r"\n\}\s*\n\s*\n\s*class",
        "\n}  // namespace device_detail\n\nusing namespace device_detail;\n\nclass",
        anon2,
        count=1,
    )
    # If class isn't in anon string, just close detail at end of anon
    if "using namespace device_detail" not in anon2:
        anon2 = anon2.rstrip() + "\n}  // namespace device_detail\n\nusing namespace device_detail;\n"

    # Fix: original has `namespace {\n ... helpers ... \n}\n\nclass` — class is NOT in preamble
    # preamble ends before class, so anon is `namespace { helpers }` only
    anon_raw = preamble[ns.end() :] if ns else ""
    # anon_raw typically: "\nnamespace {\n...\n}\n\n"
    if "namespace {" in anon_raw or anon_raw.lstrip().startswith("namespace {"):
        # replace first anonymous namespace
        anon_fix = re.sub(
            r"namespace\s*\{",
            "namespace device_detail {",
            anon_raw,
            count=1,
        )
        # rename closing of anon — last `}` before end that's anon close is hard;
        # original structure: namespace { ... } \n\n
        anon_fix = anon_fix.rstrip()
        if anon_fix.endswith("}"):
            anon_fix = anon_fix[:-1] + "}  // namespace device_detail\n\nusing namespace device_detail;\n"
    else:
        anon_fix = "namespace device_detail {\n" + anon_raw + "\n}  // namespace device_detail\nusing namespace device_detail;\n"

    hdr_lines = [
        "#pragma once",
        "",
        includes.rstrip(),
        "",
        "namespace engine::rhi {",
        "",
        anon_fix.rstrip(),
        "",
        f"class {class_name} final : public IDevice {{",
        " public:",
        *pub_decls,
        priv_stripped.rstrip(),
        "};",
        "",
        "}  // namespace engine::rhi",
        "",
    ]
    hdr_path = out_dir / f"{prefix}_internal.h"
    hdr_path.write_text("\n".join(hdr_lines) + "\n", encoding="utf-8")
    print(f"wrote {hdr_path.name} ({hdr_path.stat().st_size // 1024}KB)")

    for bucket, chunks in buckets.items():
        path = out_dir / f"{prefix}_{bucket}.cpp"
        body = (
            f'#include "{prefix}_internal.h"\n\n'
            f"namespace engine::rhi {{\n\n"
            + "\n".join(chunks)
            + "\n}  // namespace engine::rhi\n"
        )
        path.write_text(body, encoding="utf-8")
        print(f"wrote {path.name}: {len(chunks)} methods, {body.count(chr(10))} lines")

    epi = epilogue.strip()
    epi = re.sub(r"\}\s*//\s*namespace engine::rhi\s*$", "", epi).rstrip()
    main_path = out_dir / f"{prefix}.cpp"
    main_path.write_text(
        f'#include "{prefix}_internal.h"\n\nnamespace engine::rhi {{\n\n{epi}\n\n}}  // namespace engine::rhi\n',
        encoding="utf-8",
    )
    print(f"wrote {main_path.name}: {main_path.read_text(encoding='utf-8').count(chr(10))} lines")


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in ("d3d12", "vulkan"):
        print("usage: split_device_cpp.py d3d12|vulkan")
        raise SystemExit(2)
    split_kind(sys.argv[1])
