#include "mini_test.h"

#include "engine/core/feature.h"
#include "engine/core/result.h"
#include "engine/gi/cascade_gi.h"
#include "engine/gi/probe_volume.h"
#include "engine/material/material.h"
#include "engine/media/upscaler.h"
#include "engine/media/upscaler_backends.h"
#include "engine/net/quic.h"
#include "engine/render/atmosphere.h"
#include "engine/render/quality.h"
#include "engine/render/weather.h"
#include "engine/render2d/light2d.h"
#include "engine/render2d/sprite.h"
#include "engine/vfx/gpu_particles.h"

#include <cmath>
#include <span>
#include <string>
#include <vector>

TEST_CASE("W21 CreateUpscaler honest without SDK", "[w21][media][upscaler]") {
  auto up = engine::media::CreateUpscaler();
  REQUIRE(up);
  REQUIRE(std::string(up->name()) == "builtin_bilinear");
  REQUIRE_FALSE(engine::media::TryCreateDlssUpscaler());
  REQUIRE_FALSE(engine::media::TryCreateFsr2Upscaler());
}

TEST_CASE("W21 Quic loopback SKIP without ENGINE_WITH_MSQUIC API", "[w21][quic]") {
  engine::ClearFeatureOverrides();
  const auto st = engine::net::TryQuicLoopbackReliableSendRecv();
#if defined(ENGINE_WITH_MSQUIC) && ENGINE_WITH_MSQUIC
  if (!st) {
    REQUIRE(st.code() == engine::ErrorCode::Unavailable);
    REQUIRE(st.message().find("SKIP") != std::string::npos);
  }
#else
  REQUIRE_FALSE(st);
  REQUIRE(st.code() == engine::ErrorCode::Unavailable);
  REQUIRE(st.message().find("SKIP") != std::string::npos);
#endif
  engine::ClearFeatureOverrides();
}

TEST_CASE("W21 CascadeGi sample and atlas", "[w21][gi][cascade]") {
  engine::gi::CascadeGiVolume vol;
  engine::gi::CascadeGiDesc desc;
  desc.origin = {0.f, 0.f, 0.f};
  desc.extent = {4.f, 2.f, 4.f};
  desc.cascade_count = 2;
  desc.base_nx = 4;
  desc.base_ny = 3;
  desc.base_nz = 4;
  desc.sdf_occlusion = 0.5f;
  vol.Configure(desc);
  REQUIRE(vol.cascade_count() == 2);

  engine::gi::CascadeOccluderAabb box;
  box.min_p = {1.f, 0.f, 1.f};
  box.max_p = {2.f, 1.f, 2.f};
  vol.set_occluders(std::span<const engine::gi::CascadeOccluderAabb>(&box, 1));

  engine::gi::ProbeLight L;
  L.position = {0.5f, 0.5f, 0.5f};
  L.intensity = 2.f;
  L.range = 5.f;
  vol.set_budget_per_frame(64);
  for (int i = 0; i < 8; ++i) {
    vol.TickProduct(std::span<const engine::gi::ProbeLight>(&L, 1), 0.2f);
  }
  const auto c = vol.Sample({0.5f, 0.5f, 0.5f});
  REQUIRE(std::isfinite(c.r));
  REQUIRE(c.r + c.g + c.b > 0.01f);
  const auto atlas = vol.BuildIrradianceAtlasCpu();
  REQUIRE(atlas.size() == static_cast<std::size_t>(vol.primary().grid_nx() * vol.primary().grid_ny() *
                                                   vol.primary().grid_nz() * 3));
}

TEST_CASE("W21 Light2D modulates sprite color", "[w21][2d][light2d]") {
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].position = {100.f, 100.f};
  sprites[0].size = {32.f, 32.f};
  sprites[0].color = {1.f, 1.f, 1.f, 1.f};
  sprites[0].has_normals = true;
  sprites[0].layer_mask = 1u;

  engine::render2d::Light2D lamp;
  lamp.position = {116.f, 116.f};
  lamp.energy = 2.f;
  lamp.range = 80.f;
  lamp.layer_mask = 1u;
  engine::render2d::ApplyLights2D(sprites, std::span<const engine::render2d::Light2D>(&lamp, 1));
  REQUIRE(sprites[0].color.r > 0.5f);
}

TEST_CASE("W21 Light2D no lights keeps modulate only", "[w21][2d][light2d]") {
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].color = {0.5f, 0.5f, 0.5f, 1.f};
  sprites[0].modulate = {2.f, 1.f, 1.f, 1.f};
  engine::render2d::ApplyLights2D(sprites, {});
  REQUIRE(std::fabs(sprites[0].color.r - 1.f) < 1e-4f);
}

TEST_CASE("W21 PbrMaterial emission EffectiveBaseColor", "[w21][material]") {
  engine::material::PbrMaterial m;
  m.base_color = {0.2f, 0.2f, 0.2f, 1.f};
  m.emission = {1.f, 0.f, 0.f, 1.f};
  m.emission_energy = 0.5f;
  m.transparency_mode = engine::material::PbrMaterial::TransparencyMode::Alpha;
  const auto c = engine::material::EffectiveBaseColor(m);
  REQUIRE(std::fabs(c.r - 0.7f) < 1e-4f);
  REQUIRE(engine::material::WantsAlphaBlend(m));
}

TEST_CASE("W21 GpuParticle collision and kill box", "[w21][vfx][particles]") {
  engine::vfx::GpuParticleSystem sys;
  sys.Configure({0.f, 2.f, 0.f}, 40.f, 2.f, 64);
  engine::vfx::ParticleCollisionPlane plane;
  plane.point = {0.f, 0.f, 0.f};
  plane.normal = {0.f, 1.f, 0.f};
  plane.bounce = 0.5f;
  plane.enabled = true;
  sys.set_collision_plane(plane);
  engine::vfx::ParticleKillBox box;
  box.min_p = {-10.f, -1.f, -10.f};
  box.max_p = {10.f, 20.f, 10.f};
  box.enabled = true;
  sys.set_kill_box(box);
  engine::vfx::ParticleSubEmit sub;
  sub.enabled = true;
  sub.rate = 10.f;
  sys.set_sub_emit(sub);
  for (int i = 0; i < 30; ++i) {
    REQUIRE(sys.Step(1.f / 30.f));
  }
  REQUIRE(sys.particles().size() > 0);
  for (const auto& p : sys.particles()) {
    REQUIRE(p.position.y >= -0.01f);
  }
}

TEST_CASE("W21 Weather volumetric fog High enables", "[w21][weather][fog]") {
  engine::render::WeatherSystem wx;
  wx.SetState(engine::render::WeatherState::Rain, 0.8f);
  engine::render::AtmosphereParams ap;
  const auto high = wx.MakeVolumetricFogApply(ap, {0.f, 0.2f, 1.f}, engine::render::QualityTier::High,
                                              0.02f, true, false);
  REQUIRE(high.enable_fog);
  REQUIRE(high.fog_density > 0.f);
  const auto med = wx.MakeVolumetricFogApply(ap, {0.f, 0.2f, 1.f}, engine::render::QualityTier::Medium,
                                             0.02f, true, false);
  REQUIRE_FALSE(med.enable_fog);
}
