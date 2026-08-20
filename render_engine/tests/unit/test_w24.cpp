#include "mini_test.h"

#include "engine/core/feature.h"
#include "engine/gpu_driven/virtual_geometry.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/quality.h"
#include "engine/rt/raytracing.h"

#include <string>
#include <vector>

TEST_CASE("W24 Quality Medium enables SSR GTAO soft shadow VG", "[w24][quality]") {
  const auto q = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Medium);
  REQUIRE(q.enable_ssr);
  REQUIRE(q.enable_gtao);
  REQUIRE(q.enable_soft_shadow);
  REQUIRE(q.enable_virtual_geometry);
  REQUIRE(q.enable_cascade_gi);
}

TEST_CASE("W24 Quality High enables RT reflection and raytracing", "[w24][quality]") {
  const auto q = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  REQUIRE(q.enable_raytracing);
  REQUIRE(q.enable_rt_reflection);
  REQUIRE(q.enable_ssr);
}

TEST_CASE("W24 VirtualGeometry Feature and select", "[w24][gpu_driven]") {
  engine::SetFeatureOverride("virtual_geometry", true);
  REQUIRE(engine::QueryFeature("virtual_geometry"));
  std::vector<engine::Vec3> pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3};
  auto asset = engine::gpu_driven::BuildVirtualGeometry(pos, idx, 2);
  REQUIRE(!asset.nodes.empty());
}

TEST_CASE("W24 CharacterMoveEx and trigger overlaps", "[w24][physics]") {
  auto world = engine::physics::CreateDefaultPhysicsWorld();
  REQUIRE(world);
  engine::physics::CapsuleDesc cap;
  cap.position = {0, 1, 0};
  cap.mass = 0.f;
  const int ch = world->CreateCapsule(cap);
  REQUIRE(ch >= 0);
  engine::physics::IPhysicsWorld::CharacterMoveParams p;
  p.max_step_height = 0.3f;
  REQUIRE(world->MoveCharacterEx(ch, {0.1f, 0.f, 0.f}, p));

  engine::physics::RigidBodyDesc trig;
  trig.position = {0, 1, 0};
  trig.is_trigger = true;
  trig.mass = 0.f;
  const int tid = world->CreateBox(trig);
  REQUIRE(tid >= 0);
  if (std::string(world->backend_name()) == "jolt") {
    REQUIRE(world->IsBodyTrigger(tid));
    const auto hits = world->QueryTriggerOverlaps({0, 1, 0}, {2, 2, 2});
    REQUIRE(!hits.empty());
  }
}

TEST_CASE("W24 Product soft shadow compose", "[w24][rt]") {
  std::vector<float> grid;
  int w = 0, h = 0;
  auto st = engine::rt::TryProductSoftShadowMask(grid, w, h);
  REQUIRE(st);
  REQUIRE(w > 0);
  REQUIRE(!grid.empty());
}
