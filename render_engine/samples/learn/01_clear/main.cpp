#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/rhi/i_device.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path ExeDir() {
  char buf[MAX_PATH]{};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    return std::filesystem::current_path();
  }
  return std::filesystem::path(buf).parent_path();
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "render_engine — Clear";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.12f, 0.22f, 0.38f, 1.f};

  bool mesh = false;
  // Optional: --backend=vulkan (requires ENGINE_WITH_VULKAN).
  // Optional: --mesh draws a lit cube after Clear (Vulkan SPIR-V path).
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend=vulkan") == 0) {
      desc.backend = engine::rhi::Backend::Vulkan;
      desc.window.title = "render_engine M17 — Vulkan Clear";
    } else if (std::strcmp(argv[i], "--mesh") == 0) {
      mesh = true;
    }
  }
  if (mesh && desc.backend == engine::rhi::Backend::Vulkan) {
    desc.window.title = "render_engine M17 — Vulkan Lit Cube";
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  bool lit_ready = false;
  bool lit_failed = false;

  const auto status = app.value()->Run([&](engine::Application& a) {
    if (!mesh || lit_failed) {
      return;
    }
    if (!lit_ready) {
      const auto shader_dir = ExeDir() / "shaders";
      engine::rhi::LitMeshShaders shaders{};
      shaders.vs_dxil = shader_dir / "lit_cube_vk.vs.spv";
      shaders.ps_dxil = shader_dir / "lit_cube_vk.ps.spv";
      if (auto st = a.device().SetupLitMesh(shaders); !st) {
        engine::LogError(std::string("SetupLitMesh: ") + st.message());
        lit_failed = true;
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
    lighting.sun_intensity = 2.2f;
    lighting.ambient = {0.10f, 0.11f, 0.14f, 1.f};
    lighting.sun_color = {1.f, 0.96f, 0.9f, 1.f};
    lighting.specular_power = 64.f;
    lighting.enable_shadows = false;
    if (auto st = a.device().SetFrameLighting(lighting); !st) {
      engine::LogError(st.message());
      lit_failed = true;
      return;
    }

    engine::rhi::LitDrawItem item{};
    item.world = engine::Mat4::TRS({0.f, 0.f, 0.f}, engine::Quat::FromEulerYxz(0.4f, 0.25f, 0.f),
                                   {1.f, 1.f, 1.f});
    item.color = {0.85f, 0.55f, 0.35f, 1.f};
    item.metallic = 0.08f;
    item.roughness = 0.4f;
    item.use_albedo = false;
    item.use_orm = false;
    if (auto st = a.device().DrawLitCube(item); !st) {
      engine::LogError(st.message());
      lit_failed = true;
    }
  });

  if (!status) {
    engine::LogError(status.message());
    return 1;
  }
  return 0;
}
