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
#include "engine/render/local_lights.h"
#include "engine/render/quality.h"
#include "engine/render/render_scene.h"
#include "engine/render/shadow_atlas.h"
#include "engine/render/shadow_csm.h"
#include "engine/rhi/submit_config.h"
#include "engine/render2d/sprite.h"
#include "engine/scene/serialization.h"
#include "engine/scene/world.h"
#include "engine/ui/retained_ui.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

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

TEST_CASE("Local shadow matrix finite", "[render][m11]") {
  engine::render::LocalLight light;
  light.position = {2.f, 4.f, 1.f};
  light.range = 15.f;
  const auto vp = engine::render::BuildLocalShadowMatrix(light);
  REQUIRE(std::isfinite(vp.m[0]));
  REQUIRE(std::isfinite(vp.m[15]));
}

TEST_CASE("Local shadow cube matrices finite", "[render][m11]") {
  engine::render::LocalLight light;
  light.position = {2.f, 4.f, 1.f};
  light.range = 15.f;
  const auto faces = engine::render::BuildLocalShadowCubeMatrices(light);
  REQUIRE(faces.size() == 6);
  for (const auto& vp : faces) {
    REQUIRE(std::isfinite(vp.m[0]));
    REQUIRE(std::isfinite(vp.m[15]));
  }
  // Distinct look directions should yield distinct matrices.
  REQUIRE_FALSE(faces[0].m[12] == faces[1].m[12] && faces[0].m[14] == faces[1].m[14]);
}

TEST_CASE("Local light shadow atlas packs", "[render][m11]") {
  engine::render::ShadowAtlas atlas(2048);
  engine::render::LocalLightShadowScheduler sched;
  engine::render::LocalLight a;
  a.id = 1;
  a.shadow_resolution = 512;
  engine::render::LocalLight b;
  b.id = 2;
  b.shadow_resolution = 1024;
  engine::render::LocalLight c;
  c.id = 3;
  c.cast_shadows = false;
  c.shadow_resolution = 512;
  sched.AddLight(a);
  sched.AddLight(b);
  sched.AddLight(c);
  REQUIRE(sched.Pack(atlas));
  REQUIRE(sched.packed_count() == 2);
  REQUIRE(atlas.slots().size() == 2);
  // Larger tile packed first.
  REQUIRE(atlas.slots()[0].w == 1024);
}

TEST_CASE("ResolveMeshMaterial ground vs metal", "[render][material]") {
  const auto g = engine::render::ResolveMeshMaterial("ground");
  const auto m = engine::render::ResolveMeshMaterial("metal");
  REQUIRE(g.metallic < 0.1f);
  REQUIRE(m.metallic > 0.5f);
  REQUIRE_FALSE(g.albedo_tex.empty());
  REQUIRE(g.mesh_slot == 4);
}

TEST_CASE("ResolveMeshMaterial glass is transparent", "[render][material]") {
  const auto glass = engine::render::ResolveMeshMaterial("glass");
  REQUIRE(glass.transparent);
  REQUIRE(glass.base_color.a < 1.f);
}

TEST_CASE("Quality tiers differ", "[render]") {
  const auto low = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  const auto high = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  REQUIRE(low.shadow_cascades < high.shadow_cascades);
  REQUIRE_FALSE(low.enable_ssao);
  REQUIRE(high.enable_ssao);
  REQUIRE_FALSE(low.enable_ssr);
  REQUIRE_FALSE(high.enable_ssr);  // SSR default-off until floor-stable
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

TEST_CASE("CPU morph targets blend deltas", "[animation]") {
  const std::vector<engine::Vec3> bind{{0, 0, 0}, {1, 0, 0}};
  engine::animation::MorphTarget smile;
  smile.name = "smile";
  smile.deltas = {{0, 0.5f, 0}, {0, 0.25f, 0}};
  engine::animation::MorphTarget frown;
  frown.name = "frown";
  frown.deltas = {{0, -0.5f, 0}, {0, 0, 0}};
  std::vector<engine::Vec3> out;
  engine::animation::ApplyMorphTargets(bind, {smile, frown}, {1.f, 0.5f}, out);
  REQUIRE(out.size() == 2);
  REQUIRE(out[0].y > 0.2f);   // 0.5 + 0.5*(-0.5) = 0.25
  REQUIRE(out[1].y > 0.2f);   // 0.25
}

TEST_CASE("SubmitConfig validates multithread workers", "[rhi]") {
  engine::rhi::SubmitConfig ok;
  ok.multithread = true;
  ok.worker_count = 2;
  REQUIRE(engine::rhi::ValidateSubmitConfig(ok));
  engine::rhi::SubmitConfig bad;
  bad.multithread = true;
  bad.worker_count = 0;
  REQUIRE_FALSE(engine::rhi::ValidateSubmitConfig(bad));
  REQUIRE_FALSE(engine::QueryFeature("bindless"));
  REQUIRE_FALSE(engine::QueryFeature("hdr_output"));
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
  REQUIRE(world->body_count() == 1);
  REQUIRE(std::fabs(world->body_half_extents(id).x - 0.5f) < 1e-5f);
  for (int i = 0; i < 120; ++i) {
    world->Step(1.f / 60.f);
  }
  REQUIRE(world->body_position(id).y < 1.1f);
  const auto hit = world->Raycast({0, 10, 0}, {0, -1, 0}, 100.f);
  REQUIRE(hit.hit);
  REQUIRE(hit.body_id == id);
}

TEST_CASE("Default physics world and Jolt factory", "[physics][m12]") {
  auto world = engine::physics::CreateDefaultPhysicsWorld();
  REQUIRE(world);
#if defined(ENGINE_WITH_JOLT) && ENGINE_WITH_JOLT
  REQUIRE(std::string(world->backend_name()) == "jolt");
  auto jolt = engine::physics::CreateJoltPhysicsWorld();
  REQUIRE(jolt);
  engine::physics::RigidBodyDesc box;
  box.position = {0, 5, 0};
  box.half_extents = {0.5f, 0.5f, 0.5f};
  const int id = jolt->CreateBox(box);
  REQUIRE(id >= 0);
  for (int i = 0; i < 180; ++i) {
    jolt->Step(1.f / 60.f);
  }
  REQUIRE(jolt->body_position(id).y < 1.25f);
  const auto hit = jolt->Raycast({0, 10, 0}, {0, -1, 0}, 100.f);
  REQUIRE(hit.hit);
  REQUIRE(hit.body_id == id);
#else
  REQUIRE(std::string(world->backend_name()) == "builtin");
  auto jolt = engine::physics::CreateJoltPhysicsWorld();
  REQUIRE_FALSE(jolt);
#endif
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

TEST_CASE("Net HTTPS without OpenSSL is Unavailable", "[net][httplib]") {
  engine::net::NetSystem net;
  bool done = false;
  engine::Status st = engine::Status::Ok();
  std::string err;
  // Do not hit the network: https:// without OpenSSL must fail locally with Unavailable.
  REQUIRE(net.http().Get("https://example.invalid/", [&](engine::Status s, engine::net::HttpResponse resp) {
    done = true;
    st = s;
    err = resp.error.empty() ? s.message() : resp.error;
  }));
  net.Pump();
  REQUIRE(done);
#if defined(ENGINE_WITH_HTTPLIB) && ENGINE_WITH_HTTPLIB
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE(err.find("HTTPS") != std::string::npos || err.find("OpenSSL") != std::string::npos ||
          err.find("https") != std::string::npos);
#else
  (void)err;
#endif
#else
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
#endif
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
