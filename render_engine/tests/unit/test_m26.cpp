#include "mini_test.h"

#include "engine/animation/state_machine.h"
#include "engine/assets/shader_hot_reload.h"
#include "engine/core/feature.h"
#include "engine/gpu_driven/path.h"
#include "engine/render/local_lights.h"

#include <filesystem>
#include <fstream>

TEST_CASE("Local light capacity constants", "[m26][c02]") {
  static_assert(engine::render::kMaxLocalLightsGpu == 8);
  static_assert(engine::render::kMaxLocalLightsCpu == 16);
  static_assert(engine::render::kMaxLocalLightsGpu > 4);
  static_assert(engine::render::kMaxLocalShadowLights == 2);
  REQUIRE(engine::render::kMaxLocalLightsGpu == 8);
}

TEST_CASE("AnimationStateMachine samples clip and transitions", "[m26][c10]") {
  engine::animation::Skeleton skel;
  skel.joints.push_back({"root", -1, engine::Mat4::Identity()});

  engine::animation::AnimationClip idle;
  idle.name = "idle";
  idle.duration = 1.f;
  idle.tracks.resize(1);
  idle.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  idle.tracks[0].push_back({1.f, engine::Quat::Identity(), {0, 1, 0}});

  engine::animation::AnimationClip run;
  run.name = "run";
  run.duration = 0.5f;
  run.tracks.resize(1);
  run.tracks[0].push_back({0.f, engine::Quat::Identity(), {0, 0, 0}});
  run.tracks[0].push_back({0.5f, engine::Quat::Identity(), {1, 0, 0}});

  engine::animation::AnimationStateMachine sm;
  sm.AddState({"idle", idle, true});
  sm.AddState({"run", run, true});
  sm.AddTransition({"idle", "run", 0.5f, true, "StartRun"});
  sm.SetState("idle");
  sm.Update(0.6f);
  REQUIRE(sm.current_state() == "idle");
  sm.SetTrigger("StartRun");
  sm.Update(0.f);
  REQUIRE(sm.current_state() == "run");
  const auto pose = sm.Sample(skel);
  REQUIRE(pose.bone_matrices.size() == 1);
}

TEST_CASE("ShaderHotReload Poll baselines then detects change", "[m26][c16]") {
  const auto dir = std::filesystem::temp_directory_path() / "engine_m26_shader_hot";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir);
  const auto file = dir / "dummy.hlsl";
  {
    std::ofstream out(file);
    out << "// v1\n";
  }
  engine::assets::ShaderHotReload hot;
  hot.SetShaderDir(dir);
  REQUIRE_FALSE(hot.Poll());
  REQUIRE_FALSE(hot.Poll());
  // Prefer add/remove over in-place rewrite — Win mtime can be coarse.
  {
    std::ofstream out(dir / "extra.cso");
    out << "cso";
  }
  REQUIRE(hot.Poll());
  REQUIRE_FALSE(hot.Poll());
  std::filesystem::remove_all(dir, ec);
}

TEST_CASE("MeshShader path Feature SKIP without override", "[m26][c08]") {
  engine::ClearFeatureOverrides();
  engine::gpu_driven::GpuDrivenConfig cfg;
  cfg.enable_indirect = true;
  cfg.enable_mesh_shader = true;
  auto features = engine::QueryFeatures();
  features.level = engine::FeatureLevel::L2;
  REQUIRE(engine::gpu_driven::SelectPath(cfg, features) == engine::gpu_driven::Path::IndirectDraw);
}
