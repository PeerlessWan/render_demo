#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"

#include <cmath>
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
  r.enable_shadows = false;
  r.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  r.quality.enable_ssao = false;
  r.quality.enable_taa = false;
  return r;
}

void BuildRotatedCube(float angle, std::vector<engine::rhi::LitVertex>& verts,
                      std::vector<std::uint32_t>& indices) {
  const engine::Vec3 corners[8] = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
                                   {0.5f, 0.5f, -0.5f},  {-0.5f, 0.5f, -0.5f},
                                   {-0.5f, -0.5f, 0.5f},  {0.5f, -0.5f, 0.5f},
                                   {0.5f, 0.5f, 0.5f},   {-0.5f, 0.5f, 0.5f}};
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  engine::Vec3 rotated[8];
  for (int i = 0; i < 8; ++i) {
    rotated[i].x = corners[i].x * c - corners[i].z * s;
    rotated[i].y = corners[i].y;
    rotated[i].z = corners[i].x * s + corners[i].z * c;
  }
  static const int faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                                   {0, 4, 5}, {0, 5, 1}, {3, 2, 6}, {3, 6, 7},
                                   {0, 3, 7}, {0, 7, 4}, {1, 5, 6}, {1, 6, 2}};
  verts.clear();
  indices.clear();
  for (const auto& f : faces) {
    const auto& a0 = rotated[f[0]];
    const auto& a1 = rotated[f[1]];
    const auto& a2 = rotated[f[2]];
    const engine::Vec3 n = engine::Normalize(engine::Cross(a1 - a0, a2 - a0));
    const std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    verts.push_back({a0.x, a0.y, a0.z, n.x, n.y, n.z, 0, 0});
    verts.push_back({a1.x, a1.y, a1.z, n.x, n.y, n.z, 1, 0});
    verts.push_back({a2.x, a2.y, a2.z, n.x, n.y, n.z, 0, 1});
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
  }
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 05 — Upload Ring";
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  engine::render::RenderSystem render;
  if (auto st = render.Init(app.value()->device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = app.value()->Run([&](engine::Application& app_ref) {
    const float angle = static_cast<float>(app_ref.frame_index()) * 0.12f;
    std::vector<engine::rhi::LitVertex> verts;
    std::vector<std::uint32_t> indices;
    BuildRotatedCube(angle, verts, indices);
    if (auto st = app_ref.device().UploadLitGeometry(5, verts, indices); !st) {
      engine::LogError(st.message());
      return;
    }
    engine::LogInfo("UploadLitGeometry slot5 frame " + std::to_string(app_ref.frame_index()));

    const float aspect =
        app_ref.device().height() > 0
            ? static_cast<float>(app_ref.device().width()) /
                  static_cast<float>(app_ref.device().height())
            : 1.f;
    engine::rhi::FrameLighting lighting{};
    lighting.view_proj = app_ref.camera().view_proj_matrix(aspect);
    lighting.eye = app_ref.camera().position;
    lighting.sun_direction = engine::Normalize(engine::Vec3{0.3f, -1.f, 0.2f});
    lighting.sun_intensity = 2.f;
    lighting.ambient = {0.12f, 0.13f, 0.16f, 1.f};
    lighting.enable_shadows = false;
    if (auto st = app_ref.device().SetFrameLighting(lighting); !st) {
      engine::LogError(st.message());
      return;
    }

    engine::rhi::LitDrawItem item{};
    item.world = engine::Mat4::TRS({0.f, 0.5f, 0.f}, engine::Quat::Identity(), {1.f, 1.f, 1.f});
    item.color = {0.7f, 0.75f, 0.82f, 1.f};
    item.mesh_slot = 5;
    item.use_albedo = false;
    if (auto st = app_ref.device().DrawLitCube(item); !st) {
      engine::LogError(st.message());
    }
  });
  return status ? 0 : 1;
}
