#include "engine/app/application.h"
#include "engine/core/log.h"

#include <cstring>

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "render_engine — Clear";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.12f, 0.22f, 0.38f, 1.f};
  // Optional: --backend=vulkan (requires ENGINE_WITH_VULKAN). Sandbox still defaults to D3D12.
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--backend=vulkan") == 0) {
      desc.backend = engine::rhi::Backend::Vulkan;
      desc.window.title = "render_engine M17 — Vulkan Clear";
    }
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  const auto status = app.value()->Run();
  if (!status) {
    engine::LogError(status.message());
    return 1;
  }
  return 0;
}
