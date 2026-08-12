#include "mini_test.h"

#include "engine/animation/skeleton.h"
#include "engine/assets/streaming_budget.h"
#include "engine/core/feature.h"
#include "engine/core/math.h"
#include "engine/debug/console.h"
#include "engine/input/input_system.h"
#include "engine/net/net_system.h"
#include "engine/physics/i_physics_world.h"
#include "engine/post/post_stack.h"
#include "engine/render/camera.h"
#include "engine/render/quality.h"
#include "engine/render/render_scene.h"
#include "engine/render/shadow_csm.h"
#include "engine/render2d/sprite.h"
#include "engine/scene/serialization.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"

#include <filesystem>
#include <fstream>

TEST_CASE("Frustum rejects box behind camera", "[math]") {
  engine::render::Camera cam;
  cam.position = {0, 0, 5};
  cam.yaw = 0;
  cam.pitch = 0;
  const auto fr = cam.frustum(1.f);
  engine::Aabb behind{{ -0.5f, -0.5f, 10.f}, {0.5f, 0.5f, 11.f}};
  engine::Aabb front{{-0.5f, -0.5f, -1.f}, {0.5f, 0.5f, 0.f}};
  REQUIRE(fr.ContainsAabb(front));
  REQUIRE_FALSE(fr.ContainsAabb(behind));
}

TEST_CASE("RenderScene extract is snapshot", "[scene]") {
  engine::scene::World world;
  const auto id = world.CreateNode("box");
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "cube";
  world.set_mesh(id, mesh);
  world.UpdateTransforms();

  engine::render::Camera cam;
  cam.position = {0, 0, 4};
  auto scene = engine::render::RenderSceneExtractor::Extract(world, cam, 1.f);
  REQUIRE(scene.instances.size() == 1);

  engine::scene::Transform t = world.local_transform(id);
  t.position.x = 100.f;
  world.set_local_transform(id, t);
  world.UpdateTransforms();
  REQUIRE(scene.instances[0].world.m[12] == 0.f);
}

TEST_CASE("ActionMap fly defaults evaluate", "[input]") {
  engine::input::InputSystem input;
  input.InstallFlyCameraDefaults();
  input.set_key(engine::input::Key::W, true);
  input.set_gamepad_axis(0, 0.5f);
  input.EvaluateActions();
  REQUIRE(input.axis("MoveZ") > 0.f);
  REQUIRE(input.axis("MoveX") > 0.f);
  REQUIRE(input.action_map().is_bound("LookX"));
}

TEST_CASE("CSM splits are monotonic", "[render]") {
  const auto splits = engine::render::CascadedShadowMap::ComputeSplits(0.1f, 100.f, 3);
  REQUIRE(splits.size() == 3);
  REQUIRE(splits[0] < splits[1]);
  REQUIRE(splits[1] < splits[2]);
}

TEST_CASE("Quality tiers differ", "[render]") {
  const auto low = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  const auto high = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  REQUIRE(low.shadow_cascades < high.shadow_cascades);
  REQUIRE_FALSE(low.enable_ssr);
  REQUIRE(high.enable_ssr);
}

TEST_CASE("CPU skinning moves vertex", "[animation]") {
  engine::animation::Skeleton skel;
  engine::animation::Joint j;
  j.name = "root";
  j.parent = -1;
  skel.joints.push_back(j);

  engine::animation::AnimationClip clip;
  clip.duration = 1.f;
  clip.tracks.resize(1);
  clip.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  clip.tracks[0].push_back({1.f, engine::Quat::Identity(), {2, 0, 0}});
  const auto pose = engine::animation::SampleClip(skel, clip, 1.f);
  const int bones[4] = {0, 0, 0, 0};
  const float weights[4] = {1, 0, 0, 0};
  const auto v = engine::animation::SkinVertexCpu({0, 0, 0}, pose, bones, weights);
  REQUIRE(v.x > 1.5f);
}

TEST_CASE("PostStack respects quality", "[post]") {
  engine::post::PostStack stack;
  stack.Configure(engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low));
  REQUIRE_FALSE(stack.enabled("Bloom"));
  stack.set_enabled("Bloom", true);
  REQUIRE(stack.enabled("Bloom"));
}

TEST_CASE("StreamingBudget keeps referenced assets", "[assets]") {
  auto record = std::make_shared<engine::assets::AssetRecord>();
  record->id = engine::assets::AssetId("keep");
  engine::assets::AssetHandle external(record);
  engine::assets::StreamingBudget budget(10);
  REQUIRE(budget.Resident(engine::assets::AssetId("keep"), 100, external));
  REQUIRE(budget.Resident(engine::assets::AssetId("drop"), 100, {}));
  const auto evicted = budget.EvictIfNeeded();
  REQUIRE(evicted.size() == 1);
  REQUIRE(evicted[0].value() == "drop");
  REQUIRE(budget.used() == 100);
}

TEST_CASE("Physics stack and raycast", "[physics]") {
  auto world = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc box;
  box.position = {0, 5, 0};
  box.half_extents = {0.5f, 0.5f, 0.5f};
  const int id = world->CreateBox(box);
  for (int i = 0; i < 120; ++i) {
    world->Step(1.f / 60.f);
  }
  REQUIRE(world->body_position(id).y < 1.1f);
  const auto hit = world->Raycast({0, 10, 0}, {0, -1, 0}, 100.f);
  REQUIRE(hit.hit);
  REQUIRE(hit.body_id == id);
}

TEST_CASE("Scene serialize roundtrip", "[scene]") {
  const auto path = std::filesystem::temp_directory_path() / "render_engine_scene.json";
  engine::scene::World world;
  const auto id = world.CreateNode("hero");
  engine::scene::Transform t;
  t.position = {1, 2, 3};
  world.set_local_transform(id, t);
  engine::scene::MeshRenderer mesh;
  mesh.mesh_id = "hero.mesh";
  world.set_mesh(id, mesh);
  REQUIRE(engine::scene::SaveWorldJson(world, path));
  auto loaded = engine::scene::LoadWorldJson(path);
  REQUIRE(loaded);
  REQUIRE_FALSE(loaded->roots().empty());
  std::filesystem::remove(path);
}

TEST_CASE("Console tokenize and execute", "[debug]") {
  engine::debug::Console console;
  bool ran = false;
  console.Register("r.quality", [&](const std::vector<std::string>& args) {
    ran = args.size() >= 2 && args[1] == "high";
    return engine::Status::Ok();
  });
  REQUIRE(console.Execute("r.quality high"));
  REQUIRE(ran);
  REQUIRE_FALSE(console.Execute("missing"));
}

TEST_CASE("UI WantCapture flag", "[ui]") {
  engine::ui::RetainedUi ui;
  ui.Button("start", "Start", 10, 10, 80, 24);
  REQUIRE_FALSE(ui.want_capture());
  ui.set_want_capture(true);
  REQUIRE(ui.want_capture());
  REQUIRE(ui.widgets().size() == 1);
}

TEST_CASE("Sprite Y-sort order", "[render2d]") {
  std::vector<engine::render2d::Sprite> sprites(3);
  sprites[0].sort_layer = 0;
  sprites[0].sort_y = 5;
  sprites[1].sort_layer = 0;
  sprites[1].sort_y = 1;
  sprites[2].sort_layer = 1;
  sprites[2].sort_y = 0;
  engine::render2d::SortSprites(sprites);
  REQUIRE(sprites[0].sort_y == 1);
  REQUIRE(sprites[1].sort_y == 5);
  REQUIRE(sprites[2].sort_layer == 1);
}

TEST_CASE("Net loopback HTTP and WS after Pump", "[net]") {
  engine::net::NetSystem net;
  bool http_done = false;
  engine::Status http_st = engine::Status::Ok();
  REQUIRE(net.http().Get("loopback://ping", [&](engine::Status st, engine::net::HttpResponse resp) {
    http_done = true;
    http_st = st;
    REQUIRE(resp.status_code == 200);
  }));
  REQUIRE_FALSE(http_done);
  net.Pump();
  REQUIRE(http_done);
  REQUIRE(http_st);

  REQUIRE(net.websocket().Connect("loopback://echo"));
  bool got = false;
  net.websocket().set_on_message([&](std::string_view msg) {
    got = msg == "hi";
  });
  REQUIRE(net.websocket().Send("hi"));
  REQUIRE_FALSE(got);
  net.Pump();
  REQUIRE(got);
  REQUIRE_FALSE(net.quic().supported());
}

TEST_CASE("Feature query d3d12 on Windows", "[core]") {
#if defined(_WIN32)
  REQUIRE(engine::QueryFeature("d3d12"));
#endif
  REQUIRE_FALSE(engine::QueryFeature("quic"));
}

TEST_CASE("LodSelect picks band", "[assets]") {
  const std::vector<float> ranges{10.f, 30.f, 80.f};
  REQUIRE(engine::assets::LodSelect::SelectLevel(5.f, ranges) == 0);
  REQUIRE(engine::assets::LodSelect::SelectLevel(25.f, ranges) == 1);
  REQUIRE(engine::assets::LodSelect::SelectLevel(100.f, ranges) == 3);
}
