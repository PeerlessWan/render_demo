#include "mini_test.h"

#include "engine/gi/rtxgi.h"
#include "engine/gpu_driven/virtual_geometry.h"
#include "engine/material/material.h"
#include "engine/physics/i_physics_world.h"
#include "engine/rhi/i_device.h"
#include "engine/rt/raytracing.h"
#include "engine/vfx/gpu_particles.h"

#include <cmath>
#include <vector>

TEST_CASE("W23 VirtualGeometry build and select", "[w23][gpu_driven]") {
  std::vector<engine::Vec3> pos = {
      {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
  };
  std::vector<std::uint32_t> idx = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
  auto asset = engine::gpu_driven::BuildVirtualGeometry(pos, idx, 2);
  REQUIRE(!asset.cook.meshlets.empty());
  REQUIRE(!asset.nodes.empty());
  engine::gpu_driven::VirtualGeometryResidency res;
  res.page_budget = 16;
  auto sel = engine::gpu_driven::SelectClusters(asset, engine::Mat4::Identity(),
                                               engine::Mat4::Identity(), 2.f, &res);
  REQUIRE(!sel.visible_meshlet_ids.empty());
  std::vector<engine::gpu_driven::IndirectDrawArgs> args;
  const auto n =
      engine::gpu_driven::CullVirtualGeometryToIndirect(asset, sel, engine::Mat4::Identity(),
                                                        engine::Mat4::Identity(), args);
  REQUIRE(n == args.size());
}

TEST_CASE("W23 RTXGI TryCreate without SDK is nullptr", "[w23][gi][rtxgi]") {
  engine::gi::BindGiGpuDevice(engine::gi::GiGpuApi::None, nullptr);
  engine::gi::RtxgiVolumeDesc d;
  auto vol = engine::gi::TryCreateRtxgiVolume(d);
  REQUIRE(vol == nullptr);
}

TEST_CASE("W23 PostResolveDesc W23 knobs", "[w23][post]") {
  engine::rhi::PostResolveDesc p;
  p.enable_gtao = true;
  p.enable_fxaa = true;
  p.enable_color_grading = true;
  p.enable_fog_box = true;
  p.fog_box_min = {0, 0, 0};
  p.fog_box_max = {2, 2, 2};
  REQUIRE(p.NeedsResolve());
}

TEST_CASE("W23 material detail GPU helpers", "[w23][material]") {
  engine::material::PbrMaterial m;
  m.detail_albedo_tex = "detail.png";
  m.detail_blend = 0.4f;
  m.triplanar = true;
  REQUIRE(engine::material::GpuDetailBlend(m) > 0.f);
  REQUIRE(engine::material::GpuTriplanar(m) > 0.5f);
}

TEST_CASE("W23 particle mesh collision", "[w23][vfx]") {
  engine::vfx::GpuParticleSystem sys;
  sys.Configure({0, 1, 0}, 20.f, 0.5f, 32);
  engine::vfx::ParticleMeshCollision mesh;
  mesh.enabled = true;
  mesh.positions = {{ -2, 0, -2}, {2, 0, -2}, {2, 0, 2}, {-2, 0, 2}};
  mesh.indices = {0, 1, 2, 0, 2, 3};
  mesh.bounce = 0.5f;
  sys.set_mesh_collision(mesh);
  REQUIRE(sys.Step(0.05f));
}

TEST_CASE("W23 physics joints vehicle shatter", "[w23][physics]") {
  auto world = engine::physics::CreateDefaultPhysicsWorld();
  REQUIRE(world);
  engine::physics::RigidBodyDesc a;
  a.position = {0, 1, 0};
  a.mass = 1.f;
  const int ba = world->CreateBox(a);
  a.position = {1, 1, 0};
  const int bb = world->CreateBox(a);
  REQUIRE(ba >= 0);
  REQUIRE(bb >= 0);
  engine::physics::IPhysicsWorld::JointDesc j;
  j.body_a = ba;
  j.body_b = bb;
  const int jid = world->CreateJoint(j);
  // Jolt records joints; builtin returns -1 — both honest.
  if (std::string(world->backend_name()) == "jolt") {
    REQUIRE(jid >= 0);
    engine::physics::IPhysicsWorld::VehicleDesc v;
    v.position = {0, 2, 0};
    const int vid = world->CreateVehicle(v);
    REQUIRE(vid >= 0);
    REQUIRE(world->SetVehicleInput(vid, 0.5f, 0.1f));
    engine::physics::IPhysicsWorld::BreakableDesc br;
    br.body_id = ba;
    br.fragment_count = 4;
    REQUIRE(world->ShatterBody(br) > 0);
  } else {
    REQUIRE(jid < 0);
  }
}

TEST_CASE("W23 RT product soft shadow / reflection", "[w23][rt]") {
  std::vector<float> grid;
  int w = 0, h = 0;
  auto st = engine::rt::TryProductSoftShadowMask(grid, w, h);
  REQUIRE(st);
  REQUIRE(w > 0);
  REQUIRE(h > 0);
  REQUIRE(!grid.empty());
  std::vector<std::uint8_t> rgba;
  auto st2 = engine::rt::TryHalfResRtReflectionCompose(rgba, w, h);
  // May SKIP without DXR — either Ok or Unavailable is fine.
  if (st2) {
    REQUIRE(!rgba.empty());
  }
}
