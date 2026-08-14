#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 09 — PBR + IBL";
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  a.camera().position = {0.f, 1.6f, 4.f};
  a.camera().pitch = -0.18f;

  auto metal = a.world().CreateNode("metal_cube");
  {
    engine::scene::Transform t;
    t.position = {0.f, 0.5f, 0.f};
    a.world().set_local_transform(metal, t);
    engine::scene::MeshRenderer mesh;
    mesh.mesh_id = "metal";
    a.world().set_mesh(metal, mesh);
  }

  engine::render::Environment env;
  env.ibl_irradiance = "";
  env.ibl_prefilter = "";
  env.ibl_brdf_lut = "";
  engine::LogInfo(std::string("IBL configured: has_ibl=") + (env.has_ibl() ? "true" : "false"));

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
    const int frames = desc.headless_frames > 0 ? desc.headless_frames : 0;
    if (frames > 0 && app_ref.frame_index() >= frames) {
      if (const char* dump = std::getenv("ENGINE_GOLDEN_DUMP")) {
        if (dump[0] != '\0') {
          std::vector<std::uint8_t> rgba;
          int rw = 0;
          int rh = 0;
          if (auto st = app_ref.device().ReadbackTextureStub(rgba, rw, rh);
              st && rw > 0 && rh > 0 &&
              rgba.size() >= static_cast<std::size_t>(rw * rh * 4)) {
            std::ofstream out(dump, std::ios::binary);
            const std::uint32_t w32 = static_cast<std::uint32_t>(rw);
            const std::uint32_t h32 = static_cast<std::uint32_t>(rh);
            out.write(reinterpret_cast<const char*>(&w32), 4);
            out.write(reinterpret_cast<const char*>(&h32), 4);
            out.write(reinterpret_cast<const char*>(rgba.data()),
                      static_cast<std::streamsize>(w32 * h32 * 4));
          }
        }
      }
    }
  });
  return status ? 0 : 1;
}
