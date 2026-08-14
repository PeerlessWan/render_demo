#include "engine/app/application.h"
#include "engine/assets/asset_handle.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/render/environment.h"
#include "engine/render/instance_draw.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

namespace {

void ParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    }
  }
}

engine::render::RenderSystemDesc LitDesc() {
  const auto dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::render::RenderSystemDesc r{};
  r.lit_vs = dir / "lit_cube.vs.cso";
  r.lit_ps = dir / "lit_cube.ps.cso";
  r.shadow_vs = dir / "shadow.vs.cso";
  r.shadow_ps = dir / "shadow.ps.cso";
  r.quad_vs = dir / "quad.vs.cso";
  r.quad_ps = dir / "quad.ps.cso";
  r.post_vs = dir / "post_ssao_taa.vs.cso";
  r.post_ps = dir / "post_ssao_taa.ps.cso";
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 22 — LOD / Instancing / Streaming";
  ParseHeadless(argc, argv, desc);
  if (desc.headless_frames <= 0) {
    desc.headless_frames = 2;
  }

  const std::vector<float> lod_ranges{8.f, 24.f, 64.f};
  const int lod_near = engine::assets::LodSelect::SelectLevel(4.f, lod_ranges);
  const int lod_far = engine::assets::LodSelect::SelectLevel(80.f, lod_ranges);
  engine::LogInfo("LOD level near=" + std::to_string(lod_near) + " far=" + std::to_string(lod_far));

  engine::assets::StreamingBudget budget(1024 * 1024);
  engine::assets::AssetId id{"mesh/cube_lod0"};
  engine::assets::AssetHandle handle;
  (void)budget.Resident(id, 512 * 1024, handle);
  engine::LogInfo("Streaming used=" + std::to_string(budget.used()) +
                  " budget=" + std::to_string(budget.budget()));

  constexpr int kInstances = 256;
  std::vector<engine::render::InstanceData> instances(kInstances);
  std::vector<engine::Mat4> worlds(kInstances);
  for (int i = 0; i < kInstances; ++i) {
    const int x = i % 16;
    const int z = i / 16;
    engine::scene::Transform t;
    t.position = {static_cast<float>(x) * 1.1f - 8.f, 0.5f, static_cast<float>(z) * 1.1f - 8.f};
    worlds[static_cast<std::size_t>(i)] = engine::Mat4::TRS(t.position, t.rotation, t.scale);
    instances[static_cast<std::size_t>(i)].world = worlds[static_cast<std::size_t>(i)];
  }
  const auto inst_buf = engine::render::BuildInstanceBuffer(instances);
  engine::LogInfo("Instance buffer bytes=" + std::to_string(inst_buf.size()));

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  a.camera().position = {0.f, 12.f, 22.f};
  a.camera().pitch = -0.55f;

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    auto& device = app_ref.device();
    engine::rhi::FrameLighting lighting{};
    lighting.view_proj = app_ref.camera().view_proj_matrix(aspect);
    lighting.eye = app_ref.camera().position;
    lighting.enable_shadows = false;
    lighting.sun_intensity = 2.2f;
    if (auto st = device.SetFrameLighting(lighting); !st) {
      engine::LogError(st.message());
      return;
    }
    if (auto st = device.Clear({0.12f, 0.14f, 0.18f, 1.f}); !st) {
      engine::LogError(st.message());
      return;
    }
    if (auto st = device.UploadInstanceTransforms(worlds); !st) {
      engine::LogError(st.message());
      return;
    }
    engine::rhi::LitDrawItem proto{};
    proto.color = {0.7f, 0.75f, 0.85f, 1.f};
    proto.metallic = 0.1f;
    proto.roughness = 0.4f;
    if (auto st = device.DrawLitInstanced(proto, static_cast<std::uint32_t>(kInstances)); !st) {
      engine::LogError(st.message());
    }
    engine::LogInfo(std::string("gpu_instancing=") +
                    (engine::QueryFeature("gpu_instancing") ? "1" : "0"));
    (void)aspect;
    (void)render;
  });
  return status ? 0 : 1;
}
