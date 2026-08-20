#pragma once

#include "engine/vfx/particles.h"

#include "engine/core/result.h"

#include <vector>

namespace engine::vfx {
namespace particles_detail {

Status TryIntegrateGpuCsD3d12(std::vector<Particle>& particles, float dt);
Status TryIntegrateGpuCsVk(std::vector<Particle>& particles, float dt);

}  // namespace particles_detail
}  // namespace engine::vfx
