#pragma once

#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"

#include "world/world.h"

#include <algorithm>
#include <span>
#include <vector>

namespace mc {

void CollectVisible(const World& world, const engine::Vec3& eye, int radius,
                    std::vector<engine::rhi::LitDrawItem>* opaque,
                    std::vector<engine::rhi::LitDrawItem>* water);

// D3D12 object CB has 64 slots and wraps; instancing uses slot 63. Split so the
// world lands in OpaqueLit (pre-tonemap) instead of a post-resolve LDR draw.
struct LitSubmit {
  engine::rhi::LitDrawItem instanced_proto{};
  std::vector<engine::Mat4> instanced_worlds;
  std::vector<engine::rhi::LitDrawItem> colored;
};

void PrepareLitSubmit(const std::vector<engine::rhi::LitDrawItem>& opaque, const engine::Vec3& eye,
                      LitSubmit* out);

inline void QueueWorldDraws(engine::rhi::IDevice& device, engine::render::RenderSystem& render,
                            const LitSubmit& sub, std::span<const engine::rhi::LitDrawItem> water) {
  if (!sub.instanced_worlds.empty()) {
    (void)device.UploadInstanceTransforms(sub.instanced_worlds);
    render.SetPendingLitInstanced(sub.instanced_proto, sub.instanced_worlds);
  }
  if (!sub.colored.empty()) {
    (void)device.DrawLitCubes(sub.colored);
  }
  if (!water.empty()) {
    const std::size_t n = (std::min)(water.size(), static_cast<std::size_t>(64));
    (void)device.DrawTransparentLitCubes(water.subspan(0, n));
  }
}

}  // namespace mc
