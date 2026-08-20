#!/usr/bin/env python3
"""Split gltf/rt/particles sources; co-locate public headers; leave CMake updates to caller."""
from __future__ import annotations

import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / "engine"


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")
    print(f"wrote {path.relative_to(ROOT)} ({text.count(chr(10))} lines)")


def slice_lines(src: Path, start: int, end: int) -> str:
    """1-based inclusive line slice."""
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines(True)
    return "".join(lines[start - 1 : end])


def move_header(rel_include: str, dest_dir: Path) -> None:
    src = ENGINE / "include" / rel_include
    dst = dest_dir / Path(rel_include).name
    if not src.exists():
        if dst.exists():
            print(f"header already at {dst.relative_to(ROOT)}")
            return
        raise SystemExit(f"missing {src}")
    shutil.move(str(src), str(dst))
    print(f"moved {src.relative_to(ROOT)} -> {dst.relative_to(ROOT)}")


def split_gltf() -> None:
    src = ENGINE / "assets" / "gltf_loader.cpp"
    text = src.read_text(encoding="utf-8")
    lines = text.splitlines(True)

    # Build internal header for cgltf helpers shared across TUs.
    internal = '''#pragma once

#include "engine/assets/gltf_loader.h"

#include "engine/core/log.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

namespace engine::assets {
namespace gltf_detail {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
Result<ImageRgba8> DecodeImageView(const cgltf_image& image, const IImageLoader& images,
                                   const std::filesystem::path& gltf_dir);
ImageRgba8 MrToOrm(const ImageRgba8& mr);
bool ReadAccessorFloats(const cgltf_accessor* acc, std::size_t elem_floats,
                        std::vector<float>& out);
Result<GltfMeshAsset> LoadWithCgltf(const std::filesystem::path& path, const IImageLoader& images);
Result<GltfMeshAsset> LoadGltfAllMeshNodesWithCgltf(const std::filesystem::path& path,
                                                    const IImageLoader& images);
Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshPartsWithCgltf(
    const std::filesystem::path& path, const IImageLoader& images);
#endif

}  // namespace gltf_detail
}  // namespace engine::assets
'''
    write(ENGINE / "assets" / "gltf_loader_internal.h", internal)

    # Extract anonymous-namespace helpers + LoadWithCgltf from original (lines 27-360 area)
    # We'll regenerate by transforming the original file programmatically.

    # parse.cpp: CGLTF_IMPLEMENTATION + helpers + LoadWithCgltf + LoadGltfMeshFile
    # mesh.cpp: all-nodes + Append + Assemble + LoadGltfAllMeshNodes
    # skin.cpp: skinned parts + LoadGltfSkinnedMeshParts

    # Find markers in original
    def find(substr: str, start: int = 0) -> int:
        idx = text.find(substr, start)
        if idx < 0:
            raise SystemExit(f"marker not found: {substr!r}")
        return idx

    # Simpler approach: keep one implementation file split by copying ranges from line numbers
    # From earlier read:
    # helpers+LoadWithCgltf: lines 27-360 inside anon ns, ends before `#endif // ENGINE_WITH_CGLTF` at 362
    # LoadGltfMeshFile: 366-375
    # LoadGltfAllMeshNodesWithCgltf: 378-502
    # LoadGltfSkinnedMeshPartsWithCgltf: 504-658
    # LoadGltfAllMeshNodes: 661-670
    # LoadGltfSkinnedMeshParts: 672-681
    # AppendTransformedMesh: 683-710
    # AssembleGltfMeshes: 712-731

    common_includes = '''#include "engine/assets/gltf_loader.h"
#include "engine/assets/gltf_loader_internal.h"

'''

    parse = common_includes + '''#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

namespace engine::assets {
namespace gltf_detail {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF

''' + slice_lines(src, 29, 360) + '''
#endif  // ENGINE_WITH_CGLTF

}  // namespace gltf_detail

Result<GltfMeshAsset> LoadGltfMeshFile(const std::filesystem::path& path,
                                       const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
  return gltf_detail::LoadWithCgltf(path, images);
#else
  (void)path;
  (void)images;
  return Result<GltfMeshAsset>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

}  // namespace engine::assets
'''
    # Fix: slice 29-360 still has `Result<GltfMeshAsset> LoadWithCgltf` without namespace qualify - it's inside gltf_detail - good
    # But DecodeImageView etc. were in anonymous namespace - now in gltf_detail - good
    write(ENGINE / "assets" / "gltf_loader_parse.cpp", parse)

    mesh = common_includes + '''namespace engine::assets {
namespace gltf_detail {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF

''' + slice_lines(src, 378, 502) + '''
#endif

}  // namespace gltf_detail

Result<GltfMeshAsset> LoadGltfAllMeshNodes(const std::filesystem::path& path,
                                           const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
  return gltf_detail::LoadGltfAllMeshNodesWithCgltf(path, images);
#else
  (void)path;
  (void)images;
  return Result<GltfMeshAsset>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

''' + slice_lines(src, 683, 731) + '''
}  // namespace engine::assets
'''
    write(ENGINE / "assets" / "gltf_loader_mesh.cpp", mesh)

    skin = common_includes + '''namespace engine::assets {
namespace gltf_detail {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF

''' + slice_lines(src, 504, 658) + '''
#endif

}  // namespace gltf_detail

Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshParts(const std::filesystem::path& path,
                                                            const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
  return gltf_detail::LoadGltfSkinnedMeshPartsWithCgltf(path, images);
#else
  (void)path;
  (void)images;
  return Result<std::vector<GltfMeshAsset>>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

}  // namespace engine::assets
'''
    write(ENGINE / "assets" / "gltf_loader_skin.cpp", skin)

    # Facade keeps the original name for CMake clarity (thin).
    write(
        ENGINE / "assets" / "gltf_loader.cpp",
        '// glTF loader facade — implementations in gltf_loader_{parse,mesh,skin}.cpp\n'
        '#include "engine/assets/gltf_loader.h"\n',
    )

    move_header("engine/assets/gltf_loader.h", ENGINE / "assets")


def split_rt() -> None:
    src = ENGINE / "rt" / "raytracing.cpp"
    text = src.read_text(encoding="utf-8")
    lines = text.splitlines(True)

    # Find line numbers for key functions via scan
    def line_of(pred) -> int:
        for i, ln in enumerate(lines, 1):
            if pred(ln):
                return i
        raise SystemExit("line not found")

    # Approximate from earlier grep:
    # Resolve ~460, soft shadow ~668, vk stub ~776, win build ~104-456

    gate = '''#include "engine/rt/raytracing.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace engine::rt {

''' + slice_lines(src, 460, 542) + '''
}  // namespace engine::rt
'''
    # 460-542 includes Resolve through RunDxrFullscreenStub start - need careful ranges

    # Use marker-based extraction from full file by function names
    write(ENGINE / "rt" / "raytracing_internal.h", '''#pragma once

#include "engine/rt/raytracing.h"

#include <filesystem>
#include <vector>

namespace engine::rt {
namespace rt_detail {

std::filesystem::path ResolveDxrLibPath(const std::filesystem::path& override_path);

#if defined(_WIN32)
Status TryBuildCubeBlasTlasAndDispatchRaysWin(const std::filesystem::path& dxr_lib_dxil);
#endif

}  // namespace rt_detail
}  // namespace engine::rt
''')

    # Keep it simpler: split into 3 files by line ranges from grep
    # Lines 1-28 includes+ns
    # Anon helpers 30-456 (win)
    # Public gate 460-630
    # Soft 668-774
    # VK 776-end

    preamble = slice_lines(src, 1, 28)

    demo = preamble + '''namespace engine::rt {
namespace {

''' + slice_lines(src, 32, 456) + '''
}  // namespace

''' + slice_lines(src, 527, 666) + '''
}  // namespace engine::rt
'''
    # DxrShadowDemo starts 527, TryCompose ends ~666
    write(ENGINE / "rt" / "raytracing_dxr_demo.cpp", demo)

    gate_cpp = preamble + '''namespace engine::rt {

''' + slice_lines(src, 460, 525) + '''
''' + slice_lines(src, 622, 630) + '''
}  // namespace engine::rt
'''
    write(ENGINE / "rt" / "raytracing_gate.cpp", gate_cpp)

    soft = preamble + '''namespace engine::rt {

''' + slice_lines(src, 668, 774) + '''
}  // namespace engine::rt
'''
    write(ENGINE / "rt" / "raytracing_soft_shadow.cpp", soft)

    vk = preamble + '''namespace engine::rt {

''' + slice_lines(src, 776, len(lines)) + '''
}  // namespace engine::rt
'''
    # Fix double closing namespace if slice already has it
    write(ENGINE / "rt" / "raytracing_vk.cpp", vk)

    write(
        ENGINE / "rt" / "raytracing.cpp",
        '// Raytracing facade — implementations in raytracing_{gate,dxr_demo,soft_shadow,vk}.cpp\n'
        '#include "engine/rt/raytracing.h"\n',
    )
    move_header("engine/rt/raytracing.h", ENGINE / "rt")


def split_particles() -> None:
    src = ENGINE / "vfx" / "gpu_particles.cpp"
    lines = src.read_text(encoding="utf-8").splitlines(True)

    write(ENGINE / "vfx" / "gpu_particles_internal.h", '''#pragma once

#include "engine/vfx/particles.h"

#include "engine/core/result.h"

#include <vector>

namespace engine::vfx {
namespace particles_detail {

Status TryIntegrateGpuCsD3d12(std::vector<Particle>& particles, float dt);
Status TryIntegrateGpuCsVk(std::vector<Particle>& particles, float dt);

}  // namespace particles_detail
}  // namespace engine::vfx
''')

    preamble = "".join(lines[0:27])  # through namespace engine::vfx {
    # anon starts 28, d3d12 85-281, stub 282-305, vk 307-696, stub 697-700, class 704-end

    d3d = '''#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace engine::vfx {
namespace particles_detail {

''' + slice_lines(src, 85, 305) + '''
}  // namespace particles_detail
}  // namespace engine::vfx
'''
    write(ENGINE / "vfx" / "gpu_particles_d3d12.cpp", d3d)

    vk = '''#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::vfx {
namespace particles_detail {

''' + slice_lines(src, 307, 700) + '''
}  // namespace particles_detail
}  // namespace engine::vfx
'''
    write(ENGINE / "vfx" / "gpu_particles_vk.cpp", vk)

    # Orchestration: rewrite Step to call particles_detail::
    orch = '''#include "engine/vfx/gpu_particles.h"
#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>

namespace engine::vfx {

''' + slice_lines(src, 704, 760) + '''
Status GpuParticleSystem::Step(float dt) {
  if (!enabled_) {
    last_path_ = "disabled";
    last_indirect_ = {};
    return Status::Ok("disabled");
  }
  dt = std::max(0.f, dt);
  emit_accum_ += rate_ * dt;
  const int burst = static_cast<int>(emit_accum_);
  if (burst > 0) {
    emit_accum_ -= static_cast<float>(burst);
    EmitCpu(burst);
  }

  if (QueryFeature("gpu_particles")) {
    Status gpu = particles_detail::TryIntegrateGpuCsD3d12(particles_, dt);
    if (!gpu) {
      gpu = particles_detail::TryIntegrateGpuCsVk(particles_, dt);
    }
    if (gpu) {
      CullDead();
      FillIndirect();
      last_path_ = gpu.message().empty() ? "gpu" : gpu.message();
      return Status::Ok(last_path_.c_str());
    }
    LogInfo(std::string("GpuParticleSystem: Feature gpu_particles on — CS path pending; "
                        "CPU integrate (honest): ") +
            gpu.message());
  }

  IntegrateCpu(dt);
  CullDead();
  FillIndirect();
  last_path_ = "cpu-fallback";
  return Status::Ok("cpu-fallback");
}

}  // namespace engine::vfx
'''
    # Read original Step body for accuracy
    step_src = slice_lines(src, 761, len(lines))
    orch = '''#include "engine/vfx/gpu_particles.h"
#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>

namespace engine::vfx {

''' + slice_lines(src, 704, 760) + step_src.replace(
        "TryIntegrateGpuCsD3d12", "particles_detail::TryIntegrateGpuCsD3d12"
    ).replace("TryIntegrateGpuCsVk", "particles_detail::TryIntegrateGpuCsVk")
    if not orch.rstrip().endswith("}"):
        orch += "\n}  // namespace engine::vfx\n"
    write(ENGINE / "vfx" / "gpu_particles.cpp", orch)
    move_header("engine/vfx/gpu_particles.h", ENGINE / "vfx")


def main() -> None:
    split_gltf()
    split_rt()
    split_particles()
    print("done")


if __name__ == "__main__":
    main()
