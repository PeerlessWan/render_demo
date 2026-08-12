#include "engine/app/application.h"
#include "engine/core/log.h"

int main() {
  engine::ApplicationDesc desc{};
  desc.window.title = "render_engine M1 — D3D12 Clear";
  desc.window.width = 1280;
  desc.window.height = 720;
  desc.clear_color = {0.12f, 0.22f, 0.38f, 1.f};

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
