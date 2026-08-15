#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"
#include "engine/ui/immediate_ui.h"
#include "engine/ui/rml_ui.h"
#include "engine/ui/retained_ui.h"

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

void BuildMainMenu(engine::ui::RetainedUi& ui) {
  ui.Clear();
  ui.Panel("menu", 40.f, 80.f, 280.f, 200.f, {0.08f, 0.09f, 0.12f, 0.94f});
  ui.Label("menu_title", "Main Menu", 56.f, 100.f);
  ui.Button("btn_play", "Play", 56.f, 140.f, 160.f, 32.f);
  ui.Toggle("opt_ssao", "SSAO", 56.f, 188.f, 160.f, 24.f, true);
  ui.Label("menu_hint", "Esc = menu", 56.f, 230.f);
}

void BuildHud(engine::ui::RetainedUi& ui, bool ssao_on) {
  ui.Clear();
  ui.Panel("hud", 12.f, 12.f, 220.f, 72.f, {0.1f, 0.12f, 0.16f, 0.85f});
  ui.Label("title", "Learn 29 HUD", 24.f, 28.f);
  ui.Toggle("opt_ssao", "SSAO", 24.f, 52.f, 120.f, 20.f, ssao_on);
}

std::vector<engine::rhi::ScreenQuad> ToScreenQuads(const std::vector<engine::ui::UiDrawRect>& rects) {
  std::vector<engine::rhi::ScreenQuad> quads;
  quads.reserve(rects.size());
  for (const auto& r : rects) {
    engine::rhi::ScreenQuad q;
    q.x0 = r.x0;
    q.y0 = r.y0;
    q.x1 = r.x1;
    q.y1 = r.y1;
    q.color = r.color;
    quads.push_back(q);
  }
  return quads;
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
  bool in_menu = true;
  bool ssao_on = true;
  BuildMainMenu(*retained);
  engine::LogInfo(std::string("Retained backend: ") + engine::ui::QueryRetainedUiBackend().name);
  engine::LogInfo("RmlUi: accepted external (thin adapter/stub is M15 100%口径)");

  engine::render::Environment env;
  engine::render::RenderSystem render;
  if (auto st = render.Init(a.device(), LitDesc()); !st) {
    engine::LogError(st.message());
    return 1;
  }

  bool esc_was = false;
  bool mouse_left_was = false;

  const auto status = a.Run([&](engine::Application& app_ref) {
    const float w = static_cast<float>(app_ref.window().width());
    const float h = static_cast<float>(app_ref.window().height());
    const auto& snap = app_ref.window().input_snapshot();

    imgui.BeginFrame(snap, w, h, app_ref.delta_time());
    if (imgui.BeginWindow("Debug", 16.f, 16.f, 220.f, 110.f)) {
      imgui.Text(in_menu ? "State: Main Menu" : "State: Playing (HUD)");
      imgui.Text("WantCapture = ImGui || Retained");
      imgui.EndWindow();
    }
    imgui.RefreshCapture();

    const bool mouse_pressed = snap.mouse_left && !mouse_left_was;
    mouse_left_was = snap.mouse_left;
    const auto events =
        retained->Pump(snap.mouse_x, snap.mouse_y, snap.mouse_left, mouse_pressed);
    for (const auto& e : events) {
      if (e.id == "btn_play" && e.type == engine::ui::UiEventType::Click) {
        in_menu = false;
        BuildHud(*retained, ssao_on);
        engine::LogInfo("Retained: Play -> HUD");
      } else if (e.id == "opt_ssao" && e.type == engine::ui::UiEventType::Toggle) {
        ssao_on = e.bool_value;
        auto fx = render.effect_tuning();
        fx.enable_ssao = ssao_on;
        render.set_effect_tuning(fx);
      }
    }

    // Esc toggles main-menu-like state (window mode).
    const bool esc_down = snap.keys[27];
    if (esc_down && !esc_was) {
      in_menu = !in_menu;
      if (in_menu) {
        BuildMainMenu(*retained);
        retained->set_bool("opt_ssao", ssao_on);
      } else {
        BuildHud(*retained, ssao_on);
      }
    }
    esc_was = esc_down;

    const bool want =
        imgui.want_capture_mouse() || imgui.want_capture_keyboard() || retained->want_capture();
    app_ref.set_ui_want_capture(want);

    const auto ui_rects = retained->BuildDrawList();
    const auto ui_quads = ToScreenQuads(ui_rects);

    const float aspect = h > 0.f ? w / h : 1.f;
    if (auto st =
            render.DrawFrame(app_ref.device(), app_ref.render_scene(), env, aspect, nullptr, &ui_quads);
        !st) {
      engine::LogError(st.message());
      return;
    }
    (void)imgui.Render(app_ref.device());
  });
  return status ? 0 : 1;
}
