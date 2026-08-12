#include "engine/app/application.h"
#include "engine/assets/path_resolver.h"
#include "engine/core/log.h"
#include "engine/rhi/i_device.h"

#include <filesystem>
#include <string>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake (compiled shader output)"
#endif

int main() {
  engine::ApplicationDesc desc{};
  desc.window.title = "render_engine M2 — Textured Triangle";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.05f, 0.07f, 0.1f, 1.f};

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  engine::assets::PathResolver paths;
  paths.AddRoot(std::filesystem::path(ENGINE_SHADER_DIR_A));

  const auto vs = paths.Resolve("triangle.vs.cso");
  const auto ps = paths.Resolve("triangle.ps.cso");
  if (!vs || !ps) {
    engine::LogError("Compiled shaders not found under ENGINE_SHADER_DIR_A");
    return 1;
  }

  engine::rhi::SimpleMeshShaders shaders;
  shaders.vs_dxil = *vs;
  shaders.ps_dxil = *ps;
  if (auto st = app.value()->device().SetupSimpleMesh(shaders); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = app.value()->Run([](engine::Application& a) {
    if (auto st = a.device().DrawSimpleMesh(); !st) {
      engine::LogError(st.message());
    }
  });
  if (!status) {
    engine::LogError(status.message());
    return 1;
  }
  return 0;
}
