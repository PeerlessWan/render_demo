#include "mini_test.h"

#include "engine/animation/anim_serialize.h"
#include "engine/animation/blend_tree.h"
#include "engine/animation/state_machine.h"
#include "engine/assets/shader_compile_hook.h"
#include "engine/clothing/garment_cloth.h"
#include "engine/core/feature.h"
#include "engine/gi/probe_volume.h"
#include "engine/hlod/billboard_impostor.h"
#include "engine/physics/i_physics_world.h"
#include "engine/rt/raytracing.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Cape cloth pins finite after Step", "[m35][w10][cloth]") {
  engine::clothing::GarmentCloth cloth;
  engine::clothing::GarmentMeshDesc desc;
  desc.kind = engine::clothing::GarmentKind::Cape;
  desc.rows = 6;
  desc.cols = 5;
  cloth.Generate(desc, {0.f, 1.6f, 0.f});

  const std::vector<engine::Vec3> shoulders = {{-0.25f, 1.6f, 0.f}, {0.25f, 1.6f, 0.f}};
  cloth.SetAttachPoints(shoulders);

  engine::clothing::CapsuleCollider body;
  body.center = {0.f, 0.9f, 0.f};
  body.radius = 0.35f;
  body.half_height = 0.55f;

  for (int i = 0; i < 60; ++i) {
    cloth.SetAttachPoints(shoulders);
    cloth.Step(1.f / 60.f, &body);
  }

  REQUIRE(cloth.AllFinite());
  REQUIRE(cloth.pinned.size() == 5);
  for (std::size_t i = 0; i < cloth.pinned.size(); ++i) {
    const int vi = cloth.pinned[i];
    REQUIRE(vi >= 0);
    REQUIRE(vi < static_cast<int>(cloth.positions.size()));
    const engine::Vec3& p = cloth.positions[static_cast<std::size_t>(vi)];
    REQUIRE(std::isfinite(p.x));
    REQUIRE(std::isfinite(p.y));
    REQUIRE(std::isfinite(p.z));
    REQUIRE(std::fabs(p.y - 1.6f) < 0.05f);
  }
}

TEST_CASE("Skirt cloth pins finite after Step", "[m35][w10][cloth]") {
  engine::clothing::GarmentCloth cloth;
  engine::clothing::GarmentMeshDesc desc;
  desc.kind = engine::clothing::GarmentKind::Skirt;
  desc.rows = 5;
  desc.cols = 6;
  cloth.Generate(desc, {0.f, 1.0f, 0.f});
  cloth.SetAttachPoints({{0.f, 1.0f, 0.f}});

  for (int i = 0; i < 45; ++i) {
    cloth.Step(1.f / 60.f, nullptr);
  }
  REQUIRE(cloth.AllFinite());
}

TEST_CASE("Garment TryWirePhysicsSoftBody soft or SKIP", "[m35][w10][cloth]") {
  auto world = engine::physics::CreateDefaultPhysicsWorld();
  REQUIRE(world);

  engine::clothing::GarmentCloth cloth;
  engine::clothing::GarmentMeshDesc desc;
  cloth.Generate(desc, {0.f, 2.f, 0.f});
  cloth.SetAttachPoints({{0.f, 2.f, 0.f}});

  const bool wired = cloth.TryWirePhysicsSoftBody(*world, {0.f, 2.f, 0.f});
  if (wired) {
    REQUIRE(cloth.soft_body_id >= 0);
    world->Step(1.f / 60.f);
    REQUIRE(cloth.SyncFromPhysics(*world));
    REQUIRE(cloth.AllFinite());
  } else {
    REQUIRE(cloth.soft_body_id < 0);
    // Verlet path still valid for demo.
    for (int i = 0; i < 30; ++i) {
      cloth.Step(1.f / 60.f, nullptr);
    }
    REQUIRE(cloth.AllFinite());
  }
}

TEST_CASE("DDGI-lite BlendNeighborhood and CascadeRefine", "[m35][w10][gi]") {
  engine::gi::ProbeVolume vol;
  vol.Configure({0, 0, 0}, {1, 1, 1}, 3, 3, 3);
  REQUIRE(vol.probes().size() == 27);

  auto& probes = vol.probes();
  for (auto& p : probes) {
    p.irradiance = {0.f, 0.f, 0.f, 1.f};
  }
  // Spike a neighbor of the center probe (index 13 in 3x3x3).
  REQUIRE(probes.size() > 14);
  probes[14].irradiance = {1.f, 0.f, 0.f, 1.f};
  const float mid_before = probes[13].irradiance.r;
  REQUIRE(mid_before == 0.f);
  vol.BlendNeighborhood(1.f);
  const float mid_after = vol.probes()[13].irradiance.r;
  REQUIRE(std::isfinite(mid_after));
  REQUIRE(mid_after > mid_before);

  const int nx0 = vol.grid_nx();
  vol.CascadeRefine({1.f, 1.f, 1.f}, 1);
  REQUIRE(vol.grid_nx() > nx0);
}

TEST_CASE("TryHalfResSoftShadowCompose Feature gate", "[m35][w10][rt]") {
  engine::ClearFeatureOverrides();
  float factor = -1.f;
  const auto off = engine::rt::TrySoftShadowCompose(factor);
  REQUIRE_FALSE(off);
  REQUIRE(off.code() == engine::ErrorCode::Unavailable);
  REQUIRE(off.message().find("SKIP") != std::string::npos);

  engine::SetFeatureOverride("raytracing", true);
  factor = -1.f;
  const auto on = engine::rt::TrySoftShadowCompose(factor);
  if (on) {
    REQUIRE(factor > 0.f);
    REQUIRE(factor <= 1.f);
    REQUIRE(on.message().find("half-res") != std::string::npos ||
            on.message().find("soft-shadow") != std::string::npos);
  } else {
    REQUIRE(on.code() == engine::ErrorCode::Unavailable);
    REQUIRE(on.message().find("SKIP") != std::string::npos);
  }
  engine::ClearFeatureOverrides();
}

TEST_CASE("TryCompileHlslWithDxc missing dxc SKIP", "[m35][w10][c16]") {
  if (!engine::assets::IsDxcOnPath()) {
    const auto st = engine::assets::TryCompileHlslWithDxc("does_not_matter.hlsl");
    REQUIRE_FALSE(st);
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE(st.message().find("SKIP") != std::string::npos);
  } else {
    // dxc present: missing file → NotFound (not SKIP); prove probe path works.
    const auto missing = engine::assets::TryCompileHlslWithDxc(
        std::filesystem::path("definitely_missing_w10_dxc_probe.hlsl"));
    REQUIRE_FALSE(missing);
    REQUIRE(missing.code() == engine::ErrorCode::NotFound);
  }
}

TEST_CASE("HLOD SerializeBakeToFile writes bake", "[m35][w10][hlod]") {
  engine::hlod::BillboardImpostor imp;
  imp.distance_threshold = 40.f;
  imp.near_mesh_id = "mesh/tree";
  const auto dir = std::filesystem::temp_directory_path() / "engine_w10_hlod_bake";
  std::filesystem::create_directories(dir);
  const auto path = dir / "bake.txt";
  const auto st =
      engine::hlod::SerializeBakeToFile(imp, {0.2f, 0.4f, 0.6f, 1.f}, path);
  REQUIRE(st);
  REQUIRE(std::filesystem::exists(path));
  std::ifstream in(path);
  std::string line;
  REQUIRE(static_cast<bool>(std::getline(in, line)));
  REQUIRE(line.find("hlod_impostor_bake") != std::string::npos);

  engine::hlod::BillboardImpostor loaded;
  const auto load = engine::hlod::LoadBakeFromFile(loaded, path);
  REQUIRE(load);
  REQUIRE(loaded.near_mesh_id == "mesh/tree");
  REQUIRE(std::fabs(loaded.distance_threshold - 40.f) < 1e-3f);

  const auto rgba_path = dir / "solid.rgba8";
  const auto rgba_st = engine::hlod::WriteSolidRgba8Bake({0.1f, 0.2f, 0.3f, 1.f}, rgba_path);
  REQUIRE(rgba_st);
  REQUIRE(std::filesystem::file_size(rgba_path) == 16);
}

TEST_CASE("VT near default Feature default off", "[m35][w10][vt]") {
  engine::ClearFeatureOverrides();
  REQUIRE(engine::QueryFeature("virtual_texture"));
  REQUIRE_FALSE(engine::QueryFeature("vt_near_default"));
  engine::SetFeatureOverride("vt_near_default", true);
  REQUIRE(engine::QueryFeature("vt_near_default"));
  engine::ClearFeatureOverrides();
}

TEST_CASE("Anim serialize WriteFile ReadFile roundtrip", "[m35][w10][anim]") {
  engine::animation::BlendTreeNode root;
  root.op = engine::animation::BlendTreeOp::Clip;
  root.clip.name = "idle";
  root.clip.duration = 1.25f;

  const auto dir = std::filesystem::temp_directory_path() / "engine_w10_anim";
  std::filesystem::create_directories(dir);
  const auto bt_path = dir / "tree.txt";
  REQUIRE(engine::animation::WriteBlendTreeFile(bt_path, root));
  engine::animation::BlendTreeNode round;
  REQUIRE(engine::animation::ReadBlendTreeFile(bt_path, round));
  REQUIRE(round.clip.name == "idle");
  REQUIRE(std::fabs(round.clip.duration - 1.25f) < 1e-5f);

  engine::animation::AnimationStateMachine sm;
  engine::animation::AnimState a;
  a.name = "A";
  a.clip.name = "a";
  a.clip.duration = 1.f;
  sm.AddState(a);
  const auto sm_path = dir / "sm.txt";
  REQUIRE(engine::animation::WriteStateMachineFile(sm_path, sm));
  engine::animation::AnimationStateMachine sm2;
  REQUIRE(engine::animation::ReadStateMachineFile(sm_path, sm2));
  REQUIRE(sm2.states().size() == 1);
  REQUIRE(sm2.states()[0].name == "A");
}
