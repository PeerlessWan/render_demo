#!/usr/bin/env python3
from pathlib import Path

bak_path = Path(r"d:\workspace\media\render_demo\render_engine\build\_split_bak\gpu_particles.cpp")
out_dir = Path(r"d:\workspace\media\render_demo\render_engine\engine\vfx")
lines = bak_path.read_text(encoding="utf-8").splitlines(True)


def find(s: str, start: int = 0) -> int:
    for i in range(start, len(lines)):
        if s in lines[i]:
            return i
    raise SystemExit(f"not found: {s}")


i_anon = find("namespace {")
i_try_d3d = find("Status TryIntegrateGpuCsD3d12(std::vector<Particle>& particles")
i_endif_d3d = None
depth_if = 0
for i in range(i_try_d3d, len(lines)):
    st = lines[i].strip()
    if st.startswith("#if"):
        depth_if += 1
    elif st.startswith("#endif"):
        depth_if -= 1
        if depth_if == 0:
            i_endif_d3d = i
            break
# The #if WIN wraps the first TryIntegrate - find the #if before try_d3d
i_if_win = i_try_d3d
for i in range(i_try_d3d, -1, -1):
    if lines[i].strip().startswith("#if defined(_WIN32)"):
        i_if_win = i
        break
# Recompute endif for that #if
depth_if = 0
for i in range(i_if_win, len(lines)):
    st = lines[i].strip()
    if st.startswith("#if"):
        depth_if += 1
    elif st.startswith("#endif"):
        depth_if -= 1
        if depth_if == 0:
            i_endif_d3d = i
            break

i_vk_if = find("#if defined(ENGINE_WITH_VULKAN)", i_endif_d3d)
depth_if = 0
i_endif_vk = None
for i in range(i_vk_if, len(lines)):
    st = lines[i].strip()
    if st.startswith("#if"):
        depth_if += 1
    elif st.startswith("#endif"):
        depth_if -= 1
        if depth_if == 0:
            i_endif_vk = i
            break

i_class = find("void GpuParticleSystem::Configure")

print("ranges", i_anon + 1, i_if_win + 1, i_endif_d3d + 1, i_vk_if + 1, i_endif_vk + 1, i_class + 1)

common = """#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

"""

win_inc = """#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

"""

# helpers: ParticleGpu + ResolveParticleCsPath + LoadFileBytes + CreateBuffer (before #if WIN TryIntegrate)
helpers = "".join(lines[i_anon + 1 : i_if_win])
d3d_block = "".join(lines[i_if_win : i_endif_d3d + 1])
d3d = (
    common
    + win_inc
    + "namespace engine::vfx {\nnamespace particles_detail {\n\n"
    + helpers
    + d3d_block
    + "\n}  // namespace particles_detail\n}  // namespace engine::vfx\n"
)
(out_dir / "gpu_particles_d3d12.cpp").write_text(d3d, encoding="utf-8", newline="\n")

vk_inc = common + "#if ENGINE_WITH_VULKAN\n#include <vulkan/vulkan.h>\n#endif\n\n"
vk_block = "".join(lines[i_vk_if : i_endif_vk + 1])
# ParticleGpu needed by VK path
pg = """struct ParticleGpu {
  float px, py, pz, life;
  float vx, vy, vz, size;
  float cr, cg, cb, ca;
};

"""
vk = (
    vk_inc
    + "namespace engine::vfx {\nnamespace particles_detail {\n\n"
    + pg
    + vk_block
    + "\n}  // namespace particles_detail\n}  // namespace engine::vfx\n"
)
(out_dir / "gpu_particles_vk.cpp").write_text(vk, encoding="utf-8", newline="\n")

orch = """#include "engine/vfx/gpu_particles.h"
#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>

namespace engine::vfx {

"""
body = "".join(lines[i_class:])
body = body.replace("TryIntegrateGpuCsD3d12", "particles_detail::TryIntegrateGpuCsD3d12")
body = body.replace("TryIntegrateGpuCsVk", "particles_detail::TryIntegrateGpuCsVk")
(out_dir / "gpu_particles.cpp").write_text(orch + body, encoding="utf-8", newline="\n")
print("ok", len(d3d.splitlines()), len(vk.splitlines()), len((orch + body).splitlines()))
