#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/ui/immediate_ui.h"
#include "engine/ui/rml_ui.h"
#include "engine/ui/retained_ui.h"

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
  desc.window.title = "Learn 29 — UI";
  ParseHeadless(argc, argv, desc);
  if (desc.headless_frames <= 0) {
    desc.headless_frames = 2;
  }

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);

  engine::ui::ImmediateUi imgui;
  engine::ui::ImmediateUiDesc ui_desc;
  ui_desc.ui_vs = shader_dir / "ui_imgui.vs.cso";
  ui_desc.ui_ps = shader_dir / "ui_imgui.ps.cso";
  if (auto st = imgui.Init(a.device(), ui_desc); !st) {
    engine::LogWarn("ImmediateUi init: " + st.message());
  } else {
    engine::LogInfo(std::string("ImmediateUi available=") + (imgui.available() ? "true" : "false"));
  }

  auto retained = engine::ui::CreateRetainedUiBackend();
  retained->Panel("hud", 12.f, 12.f, 220.f, 80.f, {0.1f, 0.12f, 0.16f, 0.85f});
  retained->Label("title", "Learn 29 UI", 24.f, 28.f);
  retained->Toggle("opt_ssao", "SSAO", 24.f, 52.f, 120.f, 20.f, true);
  engine::LogInfo(std::string("Retained backend: ") + engine::ui::QueryRetainedUiBackend().name);

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float w = static_cast<float>(app_ref.window().width());
    const float h = static_cast<float>(app_ref.window().height());
    imgui.BeginFrame(app_ref.window().input_snapshot(), w, h, app_ref.delta_time());
    if (imgui.BeginWindow("Debug", 16.f, 16.f, 200.f, 100.f)) {
      imgui.Text("Frame UI sample");
      imgui.EndWindow();
    }
    imgui.RefreshCapture();
    app_ref.set_ui_want_capture(imgui.want_capture_mouse() || imgui.want_capture_keyboard());

    const float aspect = h > 0.f ? w / h : 1.f;
    if (auto st = render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect); !st) {
      engine::LogError(st.message());
      return;
    }
    (void)imgui.Render(app_ref.device());
    (void)retained->BuildDrawList();
  });
  return status ? 0 : 1;
}
