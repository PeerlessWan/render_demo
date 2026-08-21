#include "mini_test.h"

#include "engine/gi/cascade_gi.h"
#include "engine/material/material.h"
#include "engine/media/upscaler.h"
#include "engine/media/upscaler_backends.h"
#include "engine/render/quality.h"
#include "engine/render2d/light2d.h"
#include "engine/vfx/gpu_particles.h"

#include <cmath>
#include <span>
#include <string>
#include <vector>

TEST_CASE("W22 CascadeGi staggered tick and leak suppress", "[w22][gi]") {
  engine::gi::CascadeGiVolume vol;
  engine::gi::CascadeGiDesc desc;
  desc.cascade_count = 2;
  desc.base_nx = 4;
  desc.base_ny = 3;
  desc.base_nz = 4;
  desc.leak_suppress = 0.5f;
  desc.sdf_occlusion = 0.4f;
  vol.Configure(desc);
  engine::gi::CascadeOccluderAabb box;
  box.min_p = {0.5f, 0.f, 0.5f};
  box.max_p = {1.5f, 1.f, 1.5f};
  vol.set_occluders(std::span<const engine::gi::CascadeOccluderAabb>(&box, 1));
  engine::gi::ProbeLight L;
  L.position = {0.2f, 0.5f, 0.2f};
  L.intensity = 3.f;
  L.range = 6.f;
  for (int i = 0; i < 6; ++i) {
    vol.TickProduct(std::span<const engine::gi::ProbeLight>(&L, 1), 0.15f);
  }
  REQUIRE(vol.tick_index() >= 6);
  const auto c = vol.Sample({0.3f, 0.4f, 0.3f});
  REQUIRE(std::isfinite(c.r));
  const auto blended = engine::gi::CascadeGiVolume::BlendWithReflection(c, {1.f, 1.f, 1.f, 1.f}, 0.25f);
  REQUIRE(blended.r >= c.r);
}

TEST_CASE("W22 LightOccluder2D casts shadow", "[w22][2d]") {
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].position = {100.f, 100.f};
  sprites[0].size = {20.f, 20.f};
  sprites[0].color = {1.f, 1.f, 1.f, 1.f};
  sprites[0].layer_mask = 1u;
  engine::render2d::Light2D lamp;
  lamp.position = {10.f, 110.f};
  lamp.energy = 2.f;
  lamp.range = 200.f;
  lamp.layer_mask = 1u;
  lamp.cast_shadows = true;
  engine::render2d::LightOccluder2D wall;
  wall.polygon = {{40.f, 90.f}, {60.f, 130.f}};
  wall.layer_mask = 1u;
  const float shadow = engine::render2d::SampleOccluderShadow2D({110.f, 110.f}, lamp, {&wall, 1});
  REQUIRE(shadow > 0.5f);
  engine::render2d::ApplyLights2D(sprites, {&lamp, 1}, {&wall, 1});
  // Shadowed sprite stays near ambient (low lit).
  REQUIRE(sprites[0].color.r < 1.5f);
}

TEST_CASE("W22 CanvasModulate and Quality Low gates", "[w22][2d][quality]") {
  std::vector<engine::render2d::Sprite> sprites(1);
  sprites[0].color = {1.f, 1.f, 1.f, 1.f};
  engine::render2d::CanvasModulate mod;
  mod.color = {0.5f, 0.5f, 0.5f, 1.f};
  engine::render2d::ApplyCanvasModulate(sprites, mod);
  REQUIRE(std::fabs(sprites[0].color.r - 0.5f) < 1e-4f);

  const auto low = engine::render::QualitySettings::FromTier(engine::render::QualityTier::Low);
  REQUIRE_FALSE(low.enable_cascade_gi);
  REQUIRE_FALSE(low.enable_volumetric_fog);
  REQUIRE_FALSE(low.enable_soft_shadow);
  const auto high = engine::render::QualitySettings::FromTier(engine::render::QualityTier::High);
  REQUIRE(high.enable_cascade_gi);
}

TEST_CASE("W22 PbrMaterial detail fields and particle attractor", "[w22][material][vfx]") {
  engine::material::PbrMaterial m;
  m.detail_blend = 0.5f;
  m.triplanar = true;
  m.blend_mode = engine::material::PbrMaterial::BlendMode::Add;
  REQUIRE(engine::material::EffectiveUvScale(m) == 1.f);

  engine::vfx::GpuParticleSystem sys;
  sys.Configure({0.f, 1.f, 0.f}, 20.f, 1.f, 32);
  engine::vfx::ParticleAttractor att;
  att.position = {0.f, 0.f, 0.f};
  att.strength = 8.f;
  att.radius = 5.f;
  att.enabled = true;
  sys.set_attractor(att);
  sys.set_trail_enabled(true);
  for (int i = 0; i < 20; ++i) {
    REQUIRE(sys.Step(1.f / 30.f));
  }
  REQUIRE(sys.particles().size() > 0);
}

TEST_CASE("W22 CreateUpscaler honest without vendor evaluate", "[w22][media]") {
  engine::media::BindUpscalerGpuDevice(engine::media::UpscalerGpuApi::None, nullptr);
  auto up = engine::media::CreateUpscaler();
  REQUIRE(up);
  REQUIRE(std::string(up->name()) == "builtin_bilinear");
}

TEST_CASE("W22 Upscaler bind API", "[w22][media]") {
  int marker = 1;
  engine::media::BindUpscalerGpuDevice(engine::media::UpscalerGpuApi::D3D12, &marker);
  REQUIRE(engine::media::UpscalerGpuDeviceBound());
  REQUIRE(engine::media::UpscalerBoundApi() == engine::media::UpscalerGpuApi::D3D12);
  // Unbind before CreateUpscaler: a fake device pointer must not trip vendor DLL/NGX paths.
  engine::media::BindUpscalerGpuDevice(engine::media::UpscalerGpuApi::None, nullptr);
  auto up = engine::media::CreateUpscaler();
  REQUIRE(up);
  REQUIRE(std::string(up->name()) == "builtin_bilinear");
}
