#include "engine/render/render_system.h"

#include "engine/core/log.h"

#include <vector>

namespace engine::render {

Status RenderSystem::Init(rhi::IDevice& device, const RenderSystemDesc& desc) {
  rhi::LitMeshShaders shaders;
  shaders.vs_dxil = desc.lit_vs;
  shaders.ps_dxil = desc.lit_ps;
  if (auto st = device.SetupLitMesh(shaders); !st) {
    return st;
  }
  ready_ = true;
  LogInfo("RenderSystem lit path ready");
  return Status::Ok();
}

Status RenderSystem::DrawFrame(rhi::IDevice& device, const RenderScene& scene,
                               const Environment& env, float aspect) {
  if (!ready_) {
    return Status::Fail("RenderSystem not initialized");
  }

  std::vector<rhi::LitDrawItem> items;
  items.reserve(scene.instances.size());
  for (const auto& inst : scene.instances) {
    rhi::LitDrawItem item;
    item.world = inst.world;
    item.color = {0.72f, 0.74f, 0.78f, 1.f};
    items.push_back(item);
  }

  rhi::FrameLighting lighting;
  lighting.view_proj = scene.camera.view_proj_matrix(aspect);
  lighting.sun_direction = Normalize(env.sun_direction);
  lighting.sun_intensity = env.sun_intensity;
  lighting.ambient = env.ambient;
  lighting.sun_color = env.sun_color;

  graph_.Reset();
  graph_.AddPass(
      "OpaqueLit", {}, {"Color", "Depth"},
      [&] {
        if (auto st = device.SetFrameLighting(lighting); !st) {
          LogError(st.message());
          return;
        }
        if (auto st = device.DrawLitCubes(items); !st) {
          LogError(st.message());
        }
      });
  if (auto st = graph_.Compile(); !st) {
    return st;
  }
  if (auto st = graph_.Execute(); !st) {
    return st;
  }
  last_draw_count_ = static_cast<std::uint32_t>(items.size());
  return Status::Ok();
}

}  // namespace engine::render
