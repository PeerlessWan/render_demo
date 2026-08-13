#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/input/input_system.h"
#include "engine/render/environment.h"
#include "engine/render/render_system.h"
#include "engine/rhi/i_device.h"
#include "engine/ui/retained_ui.h"

TEST_CASE("Headless lit render system draws instances", "[usable][headless]") {
  engine::ApplicationDesc desc;
  desc.headless = true;
  desc.headless_frames = 2;
  desc.window.width = 64;
  desc.window.height = 64;

  auto app = engine::Application::Create(desc);
  REQUIRE(app);

  auto id = app.value()->world().CreateNode("cube");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  app.value()->world().set_mesh(id, mesh);

  engine::render::RenderSystem render;
  // Headless SetupLitMesh ignores shader paths.
  engine::render::RenderSystemDesc rdesc;
  REQUIRE(render.Init(app.value()->device(), rdesc));

  engine::render::Environment env;
  REQUIRE(app.value()->Run([&](engine::Application& a) {
    REQUIRE(render.DrawFrame(a.device(), a.render_scene(), env, 1.f));
    REQUIRE(render.last_draw_count() >= 1);
  }));
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
