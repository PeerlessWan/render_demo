#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/rhi/i_device.h"

#include <cstdlib>
#include <filesystem>
#include <string>

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

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 04 — Lighting CBV";
  desc.window.width = 1280;
  desc.window.height = 720;
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  bool lit_ready = false;

  const auto status = app.value()->Run([&](engine::Application& a) {
    if (!lit_ready) {
      engine::rhi::LitMeshShaders shaders;
      shaders.vs_dxil = shader_dir / "lit_cube.vs.cso";
      shaders.ps_dxil = shader_dir / "lit_cube.ps.cso";
      shaders.shadow_vs_dxil = shader_dir / "shadow.vs.cso";
      shaders.shadow_ps_dxil = shader_dir / "shadow.ps.cso";
      if (auto st = a.device().SetupLitMesh(shaders); !st) {
        engine::LogError(st.message());
        return;
      }
      lit_ready = true;
    }

    const float aspect =
        a.device().height() > 0
            ? static_cast<float>(a.device().width()) / static_cast<float>(a.device().height())
            : 1.f;

    engine::rhi::FrameLighting lighting{};
    lighting.view_proj = a.camera().view_proj_matrix(aspect);
    lighting.eye = a.camera().position;
    lighting.sun_direction = engine::Normalize(engine::Vec3{0.35f, -1.f, 0.25f});
    lighting.sun_intensity = 2.4f;
    lighting.ambient = {0.10f, 0.11f, 0.14f, 1.f};
    lighting.sun_color = {1.f, 0.96f, 0.9f, 1.f};
    lighting.specular_power = 48.f;
    lighting.enable_shadows = false;
    if (auto st = a.device().SetFrameLighting(lighting); !st) {
      engine::LogError(st.message());
      return;
    }

    engine::rhi::LitDrawItem item{};
    item.world = engine::Mat4::TRS({0.f, 0.5f, 0.f},
                                   engine::Quat::FromEulerYxz(0.25f * a.frame_index(), 0.15f, 0.f),
                                   {1.f, 1.f, 1.f});
    item.color = {0.82f, 0.58f, 0.38f, 1.f};
    item.metallic = 0.05f;
    item.roughness = 0.42f;
    item.use_albedo = false;
    if (auto st = a.device().DrawLitCube(item); !st) {
      engine::LogError(st.message());
    }
  });

  return status ? 0 : 1;
}
