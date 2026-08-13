#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/core/math.h"
#include "engine/input/input_system.h"
#include "engine/render/camera.h"
#include "engine/render/environment.h"
#include "engine/render/quality.h"
#include "engine/render/render_system.h"
#include "engine/render/shadow_csm.h"
#include "engine/render2d/sprite.h"
#include "engine/rhi/i_device.h"
#include "engine/ui/immediate_ui.h"
#include "engine/ui/retained_ui.h"
#include "engine/ui/rml_ui.h"
#include "engine/platform/window.h"

#include <array>
#include <cmath>
#include <filesystem>

TEST_CASE("set_quality enables SSAO on PostStack", "[usable][ui]") {
  engine::render::RenderSystem render;
  render.set_quality(engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium));
  REQUIRE(render.effect_tuning().enable_ssao);
  REQUIRE(render.quality().enable_ssao);
  REQUIRE(render.post_stack().enabled("SSAO"));
  // TAA stays off until motion-vector reprojection exists (jitter caused frame flash).
  REQUIRE_FALSE(render.post_stack().enabled("TAA"));
}

TEST_CASE("Headless lit+shadow render system", "[usable][headless]") {
  engine::ApplicationDesc desc;
  desc.headless = true;
  desc.headless_frames = 2;
  desc.window.width = 64;
  desc.window.height = 64;

  auto app = engine::Application::Create(desc);
  REQUIRE(app);

  auto ground = app.value()->world().CreateNode("g");
  engine::scene::MeshRenderer gm;
  gm.mesh_id = "ground";
  app.value()->world().set_mesh(ground, gm);
  auto id = app.value()->world().CreateNode("cube");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  app.value()->world().set_mesh(id, mesh);

  engine::render::RenderSystem render;
  engine::render::RenderSystemDesc rdesc;
  rdesc.enable_shadows = true;
  rdesc.quality = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
  REQUIRE(render.Init(app.value()->device(), rdesc));
  REQUIRE(render.cascade_count() == 2);

  engine::render::Environment env;
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].position = {4, 4};
  sprites[0].size = {8, 8};

  bool tuned = false;
  REQUIRE(app.value()->Run([&](engine::Application& a) {
    REQUIRE(render.DrawFrame(a.device(), a.render_scene(), env, 1.f, &sprites));
    REQUIRE(render.last_draw_count() >= 1);
    REQUIRE(render.shadows_enabled());
    if (!tuned) {
      REQUIRE(render.effect_tuning().enable_ssao);
      REQUIRE(render.post_stack().enabled("SSAO"));
      auto fx = render.effect_tuning();
      fx.enable_ssao = false;
      fx.sun_intensity = 1.2f;
      render.set_effect_tuning(fx);
      REQUIRE_FALSE(render.effect_tuning().enable_ssao);
      REQUIRE_FALSE(render.post_stack().enabled("SSAO"));
      tuned = true;
    }
  }));
}

TEST_CASE("CSM frustum slice corners finite", "[usable]") {
  engine::render::Camera cam;
  cam.position = {0, 1, 5};
  std::array<engine::Vec3, 8> corners{};
  engine::render::CascadedShadowMap::FrustumSliceCorners(cam, 1.5f, 0.5f, 20.f, corners);
  for (const auto& c : corners) {
    REQUIRE(std::isfinite(c.x));
    REQUIRE(std::isfinite(c.y));
    REQUIRE(std::isfinite(c.z));
  }
  REQUIRE((corners[4] - cam.position).length() > (corners[0] - cam.position).length());
}

TEST_CASE("Orthographic light matrix finite", "[usable]") {
  const auto proj = engine::Mat4::Orthographic(-10, 10, -10, 10, 0.1f, 100.f);
  const auto view = engine::Mat4::LookAt({0, 20, 0}, {0, 0, 0}, {0, 0, 1});
  const auto vp = proj * view;
  REQUIRE(std::isfinite(vp.m[0]));
  REQUIRE(std::isfinite(vp.m[15]));
}

TEST_CASE("ActionMap save load roundtrip", "[usable]") {
  const auto path = std::filesystem::temp_directory_path() / "render_engine_actions.json";
  engine::input::ActionMap map;
  map.Bind("Jump", "Button:Space");
  map.Bind("Fire", "Button:Mouse0");
  REQUIRE(map.SaveToFile(path));
  engine::input::ActionMap loaded;
  REQUIRE(loaded.LoadFromFile(path));
  REQUIRE(loaded.is_bound("Jump"));
  REQUIRE(*loaded.binding("Fire") == "Button:Mouse0");
  std::filesystem::remove(path);
}

TEST_CASE("RetainedUi HitTest", "[usable]") {
  engine::ui::RetainedUi ui;
  ui.Button("a", "A", 0, 0, 40, 20);
  ui.Button("b", "B", 10, 10, 40, 20);
  const auto hit = ui.HitTest(15, 15);
  REQUIRE(hit);
  REQUIRE(*hit == "b");
  REQUIRE_FALSE(ui.HitTest(200, 200));
}

TEST_CASE("RetainedUi backend factory fallback", "[usable][ui]") {
  const auto info = engine::ui::QueryRetainedUiBackend();
  REQUIRE(info.name != nullptr);
  REQUIRE_FALSE(info.is_rml);
  auto backend = engine::ui::CreateRetainedUiBackend();
  REQUIRE(backend);
  backend->Button("ok", "OK", 0, 0, 40, 20);
  REQUIRE(backend->HitTest(10, 10));
}

TEST_CASE("ImmediateUi available with ImGui", "[usable][ui]") {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  engine::ui::ImmediateUi ui;
  REQUIRE(ui.available());
#else
  REQUIRE_FALSE(engine::ui::ImmediateUi{}.available());
#endif
}

TEST_CASE("Headless ImmediateUi font+draw", "[usable][ui][headless]") {
#if defined(ENGINE_WITH_IMGUI) && ENGINE_WITH_IMGUI
  engine::rhi::DeviceDesc dd;
  dd.width = 64;
  dd.height = 64;
  dd.headless = true;
  auto device = engine::rhi::CreateHeadlessDevice(dd);
  REQUIRE(device);
  engine::ui::ImmediateUi ui;
  engine::ui::ImmediateUiDesc desc;
  desc.ui_vs = "dummy.vs";
  desc.ui_ps = "dummy.ps";
  REQUIRE(ui.Init(*device.value(), desc));
  engine::WindowInputSnapshot snap;
  ui.BeginFrame(snap, 64.f, 64.f, 1.f / 60.f);
  if (ui.BeginWindow("t", 0, 0, 100, 100)) {
    ui.Text("hello");
    bool v = true;
    ui.Checkbox("c", &v);
    float f = 0.5f;
    ui.SliderFloat("s", &f, 0.f, 1.f);
  }
  ui.EndWindow();
  REQUIRE(ui.Render(*device.value()));
#else
  REQUIRE(true);
#endif
}

TEST_CASE("RetainedUi toggle and slider pump", "[usable][ui]") {
  engine::ui::RetainedUi ui;
  ui.Panel("p", 0, 0, 200, 100);
  ui.Toggle("t", "SSAO", 10, 10, 80, 24, false);
  ui.Slider("s", "Sun", 10, 50, 160, 16, 1.f, 0.f, 4.f);

  auto ev = ui.Pump(20, 20, true, true);
  REQUIRE_FALSE(ev.empty());
  REQUIRE(ui.get_bool("t") == true);
  REQUIRE(ui.want_capture());

  ev = ui.Pump(10 + 80, 50 + 8, true, true);  // mid-right of slider
  bool saw_slider = false;
  for (const auto& e : ev) {
    if (e.type == engine::ui::UiEventType::SliderChanged) {
      saw_slider = true;
      REQUIRE(e.float_value > 1.f);
    }
  }
  REQUIRE(saw_slider);
  REQUIRE(ui.BuildDrawList().size() >= 3);
}
