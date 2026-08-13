#include "mini_test.h"

#include "engine/app/application.h"
#include "engine/assets/asset_system.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/rhi/i_device.h"

// Integration-style headless smoke (no HWND / no D3D12).
TEST_CASE("Headless framegraph-like app with assets and physics", "[headless][integration]") {
  engine::ApplicationDesc desc;
  desc.headless = true;
  desc.headless_frames = 3;
  desc.window.width = 128;
  desc.window.height = 72;

  auto app_res = engine::Application::Create(desc);
  REQUIRE(app_res);
  auto& app = *app_res.value();
  app.set_net(std::make_shared<engine::net::NetSystem>());

  auto node = app.world().CreateNode("box");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  app.world().set_mesh(node, mesh);

  auto physics = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc body;
  body.position = {0, 2, 0};
  physics->CreateBox(body);

  bool http_ok = false;
  REQUIRE(app.net()->http().Get("loopback://ci", [&](engine::Status st, engine::net::HttpResponse) {
    http_ok = static_cast<bool>(st);
  }));

  REQUIRE(app.Run([&](engine::Application& a) {
    physics->Step(a.delta_time());
    REQUIRE(a.device().DispatchCompute({1, 1, 1}));
    std::vector<std::uint8_t> px;
    int w = 0, h = 0;
    REQUIRE(a.device().ReadbackTextureStub(px, w, h));
    REQUIRE(w == 128);
    REQUIRE(a.render_scene().instances.size() >= 1);
  }));
  REQUIRE(http_ok);
}
