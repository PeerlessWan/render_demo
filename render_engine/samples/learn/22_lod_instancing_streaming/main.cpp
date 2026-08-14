#include "engine/app/application.h"
#include "engine/assets/asset_handle.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/log.h"
#include "engine/render/environment.h"
#include "engine/render/instance_draw.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

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

  std::vector<engine::render::InstanceData> instances(4);
  for (int i = 0; i < 4; ++i) {
    engine::scene::Transform t;
    t.position = {static_cast<float>(i) * 1.5f, 0.5f, 0.f};
    instances[static_cast<std::size_t>(i)].world =
        engine::Mat4::TRS(t.position, t.rotation, t.scale);
  }
  const auto inst_buf = engine::render::BuildInstanceBuffer(instances);
  engine::LogInfo("Instance buffer bytes=" + std::to_string(inst_buf.size()));

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  for (int i = 0; i < 4; ++i) {
    auto node = a.world().CreateNode("cube_" + std::to_string(i));
    engine::scene::Transform t;
    t.position = {static_cast<float>(i) * 1.2f, 0.5f, 0.f};
    a.world().set_local_transform(node, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "cube";
    a.world().set_mesh(node, mesh);
  }

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float dh = static_cast<float>(app_ref.window().height());
    const float aspect = dh > 0.f ? static_cast<float>(app_ref.window().width()) / dh : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
