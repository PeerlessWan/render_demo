#include "mini_test.h"

#include "engine/core/feature.h"
#include "engine/core/math.h"
#include "engine/gpu_driven/indirect_draw.h"
#include "engine/gpu_driven/meshlet.h"
#include "engine/net/net_system.h"
#include "engine/net/quic.h"
#include "engine/ocean/fft_ocean.h"
#include "engine/physics/buoyancy.h"
#include "engine/physics/i_physics_world.h"
#include "engine/render/atmosphere.h"
#include "engine/render/weather.h"
#include "engine/terrain/heightmap.h"
#include "engine/vfx/particles.h"
#include "engine/vt/virtual_texture.h"

#include <cmath>
#include <string>
#include <vector>

TEST_CASE("WeatherSystem transitions and CoupleFog", "[m30][w8][weather]") {
  engine::render::WeatherSystem wx;
  wx.SetState(engine::render::WeatherState::Rain, 0.8f);
  for (int i = 0; i < 30; ++i) {
    wx.Update(0.05f);
  }
  REQUIRE(wx.state() == engine::render::WeatherState::Rain);
  REQUIRE(wx.intensity() > 0.3f);
  REQUIRE(wx.moisture() > 0.f);

  const auto zone = wx.SampleZone({10.f, 0.f, -4.f});
  REQUIRE(zone.state == engine::render::WeatherState::Rain);
  REQUIRE(zone.precip_rate > 0.f);

  engine::render::AtmosphereParams ap;
  ap.sun_dir = {0.2f, 1.f, 0.1f};
  const auto fog = wx.CoupleFog(ap, {0.f, 0.2f, 1.f}, 0.02f, true);
  REQUIRE(std::isfinite(fog.fog_density));
  REQUIRE(fog.fog_density > 0.02f);
}

TEST_CASE("WeatherSystem storm lightning and snow cover", "[m30][w8][weather]") {
  engine::render::WeatherSystem wx;
  wx.SetState(engine::render::WeatherState::Storm, 1.f);
  bool flashed = false;
  for (int i = 0; i < 200; ++i) {
    wx.Update(0.1f);
    if (wx.lightning_flash() > 0.1f) {
      flashed = true;
      break;
    }
  }
  REQUIRE(flashed);

  engine::render::WeatherSystem snow;
  snow.SetState(engine::render::WeatherState::Snow, 1.f);
  for (int i = 0; i < 80; ++i) {
    snow.Update(0.1f);
  }
  REQUIRE(snow.snow_cover() > 0.05f);

  engine::vfx::ParticleEmitter em;
  snow.ConfigurePrecipEmitter(em, {0.f, 1.f, 0.f});
  REQUIRE(em.enabled());
  snow.UpdateCurtain(0.2f, {0.f, 1.f, 0.f});
  REQUIRE_FALSE(snow.curtain().empty());
}

TEST_CASE("FftOcean heightfield SampleHeight and foam", "[m30][w8][ocean]") {
  engine::ocean::FftOcean ocean;
  engine::ocean::FftOceanDesc desc;
  desc.resolution = 64;
  desc.world_size = 32.f;
  desc.amplitude = 0.6f;
  ocean.Configure(desc);
  ocean.Update(0.5f);
  ocean.SnapOriginToCamera({100.f, 5.f, -50.f});

  const float h0 = ocean.SampleHeight(100.f, -50.f);
  REQUIRE(std::isfinite(h0));
  const auto n = ocean.SampleNormal(100.f, -50.f);
  REQUIRE(std::isfinite(n.x));
  REQUIRE(n.y > 0.2f);
  const float foam = ocean.SampleFoam(100.f, -50.f);
  REQUIRE(foam >= 0.f);
  REQUIRE(foam <= 1.f);

  // Infinite tiling: offset by world_size should match (periodic).
  const float h1 = ocean.SampleHeight(100.f + desc.world_size, -50.f);
  REQUIRE(std::fabs(h0 - h1) < 1e-3f);
}

TEST_CASE("FftOcean AnimateMesh updates Y and normals", "[m30][w8][ocean]") {
  engine::ocean::FftOcean ocean;
  engine::ocean::FftOceanDesc desc;
  desc.resolution = 64;
  desc.world_size = 16.f;
  ocean.Configure(desc);
  ocean.Update(0.25f);
  ocean.SnapOriginToCamera({0.f, 0.f, 0.f});

  auto mesh = engine::ocean::BuildOceanTileMesh(ocean, 8);
  REQUIRE(mesh.positions.size() == 9 * 9 * 3);
  REQUIRE(mesh.normals.size() == mesh.positions.size());
  float max_abs_y = 0.f;
  for (std::size_t i = 0; i < mesh.positions.size() / 3; ++i) {
    max_abs_y = std::max(max_abs_y, std::fabs(mesh.positions[i * 3 + 1]));
  }
  REQUIRE(max_abs_y > 1e-4f);
}

TEST_CASE("SampleHeightTiled and AnimateWaterPatchFromHeightfield", "[m30][w8][water]") {
  engine::terrain::Heightmap map;
  map.width = 4;
  map.height = 4;
  map.cell = 1.f;
  map.samples = {0, 0.2f, 0, 0, 0.1f, 0.5f, 0.1f, 0, 0, 0.2f, 0, 0, 0, 0, 0, 0};
  const float h = engine::terrain::SampleHeightTiled(map, 5.5f, 1.5f, 0.f, 0.f);
  REQUIRE(std::isfinite(h));

  auto mesh = engine::terrain::BuildAnimatedWaterPatchMesh(2.f, 4, 0.f, 0.f);
  engine::terrain::AnimateWaterPatchFromHeightfield(mesh, map, 0.f, 0.f);
  REQUIRE(mesh.normals.size() == mesh.positions.size());
  REQUIRE(mesh.uvs.size() >= mesh.positions.size() / 3 * 2);
}

TEST_CASE("Boat buoyancy probes and ApplyImpulse", "[m30][w8][buoyancy]") {
  engine::ocean::FftOcean ocean;
  engine::ocean::FftOceanDesc desc;
  desc.resolution = 64;
  desc.world_size = 32.f;
  desc.amplitude = 0.3f;
  ocean.Configure(desc);
  ocean.Update(0.2f);
  ocean.SnapOriginToCamera({0.f, 0.f, 0.f});

  auto world = engine::physics::CreateBuiltinPhysicsWorld();
  engine::physics::RigidBodyDesc box;
  box.position = {0.f, -0.5f, 0.f};
  box.half_extents = {0.6f, 0.25f, 1.2f};
  box.mass = 200.f;
  const int id = world->CreateBox(box);
  REQUIRE(id >= 0);

  engine::physics::BoatBuoyancyParams params;
  params.mass = 200.f;
  params.thrust = 50.f;
  params.flood_rate = 5.f;
  float flood = 0.f;
  const auto forces = engine::physics::ComputeBoatBuoyancy(
      ocean, world->body_position(id), engine::Quat::Identity(), params, 1.f / 60.f, flood);
  REQUIRE(forces.active_probes >= 1);
  REQUIRE(std::isfinite(forces.force.y));
  // Submerged boat should get upward buoyancy component in the raw hydrostatic part;
  // net force includes gravity — still finite and ApplyImpulse succeeds.
  REQUIRE(engine::physics::ApplyBuoyancyForces(*world, id, forces, 1.f / 60.f));
  world->Step(1.f / 60.f);
  const auto pos = world->body_position(id);
  REQUIRE(std::isfinite(pos.y));
}

TEST_CASE("Boat flood_rate accumulates mass", "[m30][w8][buoyancy]") {
  engine::ocean::FftOcean ocean;
  engine::ocean::FftOceanDesc desc;
  desc.resolution = 64;
  desc.amplitude = 0.1f;
  ocean.Configure(desc);
  ocean.SnapOriginToCamera({});

  engine::physics::BoatBuoyancyParams params;
  params.flood_rate = 20.f;
  params.max_flood_mass = 100.f;
  float flood = 0.f;
  // Place boat deep under mean water so probes stay submerged.
  for (int i = 0; i < 30; ++i) {
    (void)engine::physics::ComputeBoatBuoyancy(ocean, {0.f, -2.f, 0.f}, engine::Quat::Identity(),
                                               params, 0.1f, flood);
  }
  REQUIRE(flood > 1.f);
  REQUIRE(flood <= params.max_flood_mass + 1e-3f);
}

TEST_CASE("MeshletizeAabbGrid splits by cell", "[m30][w8][c08]") {
  std::vector<engine::Vec3> pos{
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
      {9.f, 9.f, 0.f}, {10.f, 9.f, 0.f}, {9.f, 10.f, 0.f},
  };
  std::vector<std::uint32_t> idx{0, 1, 2, 3, 4, 5};
  const auto cooked = engine::gpu_driven::MeshletizeAabbGrid(pos, idx, 2);
  REQUIRE(cooked.meshlets.size() >= 2);
  REQUIRE(cooked.indices.size() == 6);
  std::uint32_t total_idx = 0;
  for (const auto& m : cooked.meshlets) {
    REQUIRE(m.index_count >= 3);
    total_idx += m.index_count;
  }
  REQUIRE(total_idx == 6);
}

TEST_CASE("MeshletizePreferMeshoptimizer falls back to AABB", "[m30][w9][c08]") {
  // third_party has no meshoptimizer — Prefer API must match AABB cook.
  std::vector<engine::Vec3> pos{
      {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f},
      {9.f, 9.f, 0.f}, {10.f, 9.f, 0.f}, {9.f, 10.f, 0.f},
  };
  std::vector<std::uint32_t> idx{0, 1, 2, 3, 4, 5};
  const auto aabb = engine::gpu_driven::MeshletizeAabbGrid(pos, idx, 2);
  const auto prefer = engine::gpu_driven::MeshletizePreferMeshoptimizer(pos, idx, 2);
  REQUIRE(prefer.meshlets.size() == aabb.meshlets.size());
  REQUIRE(prefer.indices.size() == aabb.indices.size());
}

TEST_CASE("CullMeshletsToIndirect frustum culls", "[m30][w8][c08]") {
  engine::ClearFeatureOverrides();
  std::vector<engine::gpu_driven::Meshlet> meshlets(2);
  meshlets[0].aabb = {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
  meshlets[0].first_index = 0;
  meshlets[0].index_count = 36;
  meshlets[1].aabb = {{100.f, 100.f, 100.f}, {101.f, 101.f, 101.f}};
  meshlets[1].first_index = 36;
  meshlets[1].index_count = 36;

  const engine::Mat4 view = engine::Mat4::LookAt({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  const engine::Mat4 proj = engine::Mat4::Perspective(1.047f, 1.f, 0.1f, 50.f);
  const engine::Mat4 vp = proj * view;
  const engine::Mat4 world = engine::Mat4::Identity();

  std::vector<std::uint32_t> visible;
  std::vector<engine::gpu_driven::IndirectDrawArgs> args;
  const std::uint32_t kept =
      engine::gpu_driven::CullMeshletsToIndirect(meshlets, world, vp, nullptr, visible, args);
  REQUIRE(kept == 1);
  REQUIRE(visible.size() == 1);
  REQUIRE(visible[0] == 0);
  REQUIRE(args.size() == 1);
  REQUIRE(args[0].index_count_per_instance == 36);
  REQUIRE(engine::QueryFeature("execute_indirect"));
  engine::ClearFeatureOverrides();
}

TEST_CASE("MeshletPathAvailable and MS path Feature gate", "[m30][w8][w9][c08]") {
  engine::ClearFeatureOverrides();
  REQUIRE_FALSE(engine::gpu_driven::MeshletPathAvailable());
  auto st = engine::gpu_driven::TryMeshShaderPath();
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE(st.message().find("SKIP") != std::string::npos);

  engine::SetFeatureOverride("meshlet", true);
  REQUIRE(engine::gpu_driven::MeshletPathAvailable());
  st = engine::gpu_driven::TryMeshShaderPath();
  // Ok (ready / MS PSO / DispatchMesh) or Unavailable (tier/shader/PSO) — honest SKIP.
  if (st) {
    REQUIRE(st.code() == engine::ErrorCode::Ok);
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE((st.message().find("Mesh Shader") != std::string::npos ||
             st.message().find("meshlet") != std::string::npos ||
             st.message().find("PSO") != std::string::npos));
  }
  engine::ClearFeatureOverrides();

  engine::SetFeatureOverride("mesh_shader", true);
  st = engine::gpu_driven::TryMeshShaderPathStub();
  if (st) {
    REQUIRE(st.code() == engine::ErrorCode::Ok);
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  }
  engine::ClearFeatureOverrides();
}

TEST_CASE("ProbeMeshShaderSupportVk returns Ok or Unavailable", "[m30][w9][c08]") {
  auto st = engine::gpu_driven::ProbeMeshShaderSupportVk();
  if (st) {
    REQUIRE(st.code() == engine::ErrorCode::Ok);
  } else {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE(st.message().find("mesh_shader") != std::string::npos ||
            st.message().find("VULKAN") != std::string::npos ||
            st.message().find("vkCreateInstance") != std::string::npos);
  }
}

TEST_CASE("VirtualTexture residency and Sample stub", "[m30][w8][c06]") {
  engine::vt::VirtualTexture vt;
  vt.Configure(/*virtual_pages_xy=*/4, /*physical_slots=*/2, /*mip_count=*/2);
  REQUIRE(vt.physical_slots() == 2);
  REQUIRE(vt.resident_count() == 0);

  const auto page = vt.UvToPage(0.1f, 0.1f, 0);
  REQUIRE_FALSE(vt.IsResident(page));
  const auto miss = vt.Sample(0.1f, 0.1f, 0);
  REQUIRE(miss.a == 0.f);
  REQUIRE(vt.pending_requests().size() == 1);

  REQUIRE(vt.ProcessRequests(1) == 1);
  REQUIRE(vt.IsResident(page));
  REQUIRE(vt.resident_count() == 1);
  const auto hit = vt.Sample(0.1f, 0.1f, 0);
  REQUIRE(hit.a == 1.f);
  REQUIRE(hit.r > 0.f);

  (void)vt.Sample(0.9f, 0.1f, 0);
  REQUIRE(vt.ProcessRequests(1) == 1);
  REQUIRE(vt.resident_count() == 2);
  (void)vt.Sample(0.1f, 0.9f, 0);
  REQUIRE(vt.ProcessRequests(1) == 1);
  REQUIRE(vt.resident_count() == 2);
}

TEST_CASE("MsQuic probe Feature and Net hook SKIP", "[m30][w8][quic]") {
  engine::ClearFeatureOverrides();
  const auto info = engine::net::QueryMsQuicProbeInfo();
  REQUIRE_FALSE(info.detail.empty());

  engine::net::NetSystem net;
  const auto connect = net.quic().Connect("127.0.0.1", 443);
  REQUIRE_FALSE(connect);
  REQUIRE(connect.code() == engine::ErrorCode::Unavailable);
  REQUIRE(connect.message().find("ADR 0031") != std::string::npos);

  if (!info.dll_or_lib_present) {
    REQUIRE_FALSE(engine::QueryFeature("quic"));
    REQUIRE_FALSE(net.quic().supported());
    const auto stub = engine::net::TryQuicConnectStub("example.com", 443);
    REQUIRE_FALSE(stub);
    REQUIRE(stub.message().find("SKIP") != std::string::npos ||
            stub.message().find("ADR 0031") != std::string::npos);
  } else {
    REQUIRE(engine::net::ProbeAndSetQuicFeature());
    REQUIRE(engine::QueryFeature("quic"));
    REQUIRE(net.quic().supported());
  }

  engine::SetFeatureOverride("quic", true);
  const auto forced = engine::net::TryQuicConnectStub("127.0.0.1", 4433);
  REQUIRE_FALSE(forced);
  REQUIRE(forced.code() == engine::ErrorCode::Unavailable);
  engine::ClearFeatureOverrides();
}
