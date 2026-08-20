#include "engine/render/weather.h"

#include <algorithm>
#include <cmath>

namespace engine::render {
namespace {

float Saturate(float x) { return std::clamp(x, 0.f, 1.f); }

float Hash21(float x, float z) {
  const float n = std::sin(x * 127.1f + z * 311.7f) * 43758.5453f;
  return n - std::floor(n);
}

}  // namespace

void WeatherSystem::SetState(WeatherState state, float intensity) {
  target_state_ = state;
  target_intensity_ = Saturate(intensity);
  if (state_ == WeatherState::Clear && state != WeatherState::Clear) {
    state_ = state;
  }
}

void WeatherSystem::set_wind(const Vec3& dir, float speed) {
  wind_dir_ = Normalize(dir);
  if (wind_dir_.length_squared() < 1e-8f) {
    wind_dir_ = Vec3{1.f, 0.f, 0.f};
  }
  wind_speed_ = std::max(0.f, speed);
}

float WeatherSystem::NextRand() {
  rng_ = rng_ * 1664525u + 1013904223u;
  return static_cast<float>(rng_ & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float WeatherSystem::PrecipRate() const {
  switch (state_) {
    case WeatherState::Rain:
      return 40.f * intensity_;
    case WeatherState::Storm:
      return 90.f * intensity_;
    case WeatherState::Snow:
      return 28.f * intensity_;
    default:
      return 0.f;
  }
}

void WeatherSystem::Update(float dt) {
  dt = std::max(0.f, dt);
  time_ += dt;

  // Smooth intensity toward target; snap state when intensity crosses.
  const float step = transition_speed_ * dt;
  if (intensity_ < target_intensity_) {
    intensity_ = std::min(intensity_ + step, target_intensity_);
  } else if (intensity_ > target_intensity_) {
    intensity_ = std::max(intensity_ - step, target_intensity_);
  }
  if (std::fabs(intensity_ - target_intensity_) < 1e-3f) {
    state_ = target_state_;
  } else if (target_state_ != WeatherState::Clear && intensity_ > 0.05f) {
    state_ = target_state_;
  } else if (target_state_ == WeatherState::Clear && intensity_ < 0.05f) {
    state_ = WeatherState::Clear;
  }

  // Moisture / snow cover accumulation & decay.
  const float wet_src =
      (state_ == WeatherState::Rain || state_ == WeatherState::Storm) ? intensity_ : 0.f;
  const float snow_src = (state_ == WeatherState::Snow) ? intensity_ : 0.f;
  moisture_ = Saturate(moisture_ + (wet_src * 0.35f - 0.08f) * dt);
  if (snow_src > 0.f) {
    snow_cover_ = Saturate(snow_cover_ + snow_src * 0.12f * dt);
  } else if (state_ == WeatherState::Clear || state_ == WeatherState::Cloudy) {
    snow_cover_ = Saturate(snow_cover_ - 0.04f * dt);
  } else if (wet_src > 0.f) {
    snow_cover_ = Saturate(snow_cover_ - wet_src * 0.15f * dt);
  }

  // Storm lightning flashes.
  lightning_flash_ = std::max(0.f, lightning_flash_ - dt * 4.f);
  if (state_ == WeatherState::Storm && intensity_ > 0.2f) {
    storm_timer_ -= dt;
    if (storm_timer_ <= 0.f) {
      lightning_flash_ = 0.55f + 0.45f * NextRand();
      storm_timer_ = 1.5f + NextRand() * 4.5f;
    }
  } else {
    storm_timer_ = 0.5f;
  }

  // Wind strength scales with weather.
  if (state_ == WeatherState::Storm) {
    wind_speed_ = std::max(wind_speed_, 8.f + 10.f * intensity_);
  } else if (state_ == WeatherState::Rain) {
    wind_speed_ = std::max(wind_speed_, 4.f + 4.f * intensity_);
  }
}

WeatherZoneSample WeatherSystem::SampleZone(const Vec3& world_pos) const {
  WeatherZoneSample s;
  s.state = state_;
  const float h = Hash21(std::floor(world_pos.x * 0.05f), std::floor(world_pos.z * 0.05f));
  const float local = 0.85f + 0.3f * (h - 0.5f);
  s.intensity = Saturate(intensity_ * local);
  s.wind_speed = wind_speed_ * (0.9f + 0.2f * h);
  s.moisture = moisture_;
  s.snow_cover = snow_cover_ * (0.7f + 0.6f * Hash21(world_pos.x * 0.02f, world_pos.z * 0.02f));
  s.lightning_flash = lightning_flash_;
  s.precip_rate = PrecipRate() * local;
  return s;
}

CoupledFog WeatherSystem::CoupleFog(const AtmosphereParams& atmosphere, const Vec3& view_dir,
                                    float base_fog_density, bool enable_clouds) const {
  AtmosphereParams ap = atmosphere;
  // Weather nudges turbidity / cloud coverage.
  switch (state_) {
    case WeatherState::Cloudy:
      ap.cloud_coverage = Saturate(ap.cloud_coverage + 0.35f * intensity_);
      ap.turbidity += 0.8f * intensity_;
      break;
    case WeatherState::Rain:
      ap.cloud_coverage = Saturate(ap.cloud_coverage + 0.55f * intensity_);
      ap.turbidity += 1.4f * intensity_;
      break;
    case WeatherState::Storm:
      ap.cloud_coverage = Saturate(0.85f + 0.15f * intensity_);
      ap.turbidity += 2.2f * intensity_;
      break;
    case WeatherState::Snow:
      ap.cloud_coverage = Saturate(ap.cloud_coverage + 0.45f * intensity_);
      ap.turbidity += 1.1f * intensity_;
      break;
    default:
      break;
  }

  CoupledFog fog = CoupleFogWithAtmosphere(ap, view_dir, base_fog_density, enable_clouds);
  const float wet = moisture_ * 0.55f + intensity_ * 0.35f *
                                          (state_ == WeatherState::Rain || state_ == WeatherState::Storm
                                               ? 1.f
                                               : (state_ == WeatherState::Snow ? 0.6f : 0.15f));
  fog.fog_density *= (1.f + wet);
  if (lightning_flash_ > 0.01f) {
    const float flash = lightning_flash_;
    fog.fog_color.r = Saturate(fog.fog_color.r + flash * 0.55f);
    fog.fog_color.g = Saturate(fog.fog_color.g + flash * 0.55f);
    fog.fog_color.b = Saturate(fog.fog_color.b + flash * 0.7f);
    fog.clear_color.r = Saturate(fog.clear_color.r + flash * 0.4f);
    fog.clear_color.g = Saturate(fog.clear_color.g + flash * 0.4f);
    fog.clear_color.b = Saturate(fog.clear_color.b + flash * 0.55f);
  }
  if (state_ == WeatherState::Snow) {
    fog.fog_color.r = fog.fog_color.r * 0.9f + 0.75f * 0.1f;
    fog.fog_color.g = fog.fog_color.g * 0.9f + 0.78f * 0.1f;
    fog.fog_color.b = fog.fog_color.b * 0.9f + 0.85f * 0.1f;
  }
  return fog;
}

WeatherSystem::VolumetricFogApply WeatherSystem::MakeVolumetricFogApply(
    const AtmosphereParams& atmosphere, const Vec3& view_dir, QualityTier tier,
    float base_fog_density, bool enable_clouds, bool caller_enable_fog) const {
  const CoupledFog fog = CoupleFog(atmosphere, view_dir, base_fog_density, enable_clouds);
  VolumetricFogApply out;
  out.fog_density = fog.fog_density;
  out.fog_color = {fog.fog_color.r, fog.fog_color.g, fog.fog_color.b};
  out.clear_color = fog.clear_color;
  // High: default-on product volumetric feel; Medium/Low honor caller (don't explode FPS).
  if (tier == QualityTier::High) {
    out.enable_fog = true;
    out.fog_density = std::min(out.fog_density, 0.06f);
  } else {
    out.enable_fog = caller_enable_fog;
    if (tier == QualityTier::Medium && out.enable_fog) {
      out.fog_density = std::min(out.fog_density, 0.035f);
    }
  }
  return out;
}

void WeatherSystem::ConfigurePrecipEmitter(vfx::ParticleEmitter& emitter,
                                           const Vec3& camera_pos) const {
  const float rate = PrecipRate();
  const bool precip = rate > 0.5f;
  emitter.set_enabled(precip);
  if (!precip) {
    return;
  }
  const Vec3 origin = camera_pos + Vec3{0.f, 8.f, 0.f} + wind_dir_ * (-2.f);
  const float life = (state_ == WeatherState::Snow) ? 2.2f : 0.9f;
  emitter.Configure(origin, rate, life);
  emitter.set_origin(origin);
}

void WeatherSystem::UpdateCurtain(float dt, const Vec3& camera_pos) {
  dt = std::max(0.f, dt);
  const float rate = PrecipRate();
  const bool snow = state_ == WeatherState::Snow;
  curtain_accum_ += rate * dt;
  while (curtain_accum_ >= 1.f && curtain_.size() < 128) {
    CurtainParticle p;
    const float ox = (NextRand() - 0.5f) * 24.f;
    const float oz = (NextRand() - 0.5f) * 24.f;
    p.position = camera_pos + Vec3{ox, 6.f + NextRand() * 6.f, oz};
    if (snow) {
      p.velocity = wind_dir_ * wind_speed_ * 0.35f +
                   Vec3{(NextRand() - 0.5f) * 0.6f, -1.2f - NextRand() * 0.8f,
                        (NextRand() - 0.5f) * 0.6f};
      p.color = {0.92f, 0.94f, 1.f, 0.85f};
      p.size = 3.f + NextRand() * 3.f;
      p.life = 1.8f + NextRand() * 1.2f;
    } else {
      p.velocity = wind_dir_ * wind_speed_ * 0.55f + Vec3{0.f, -12.f - NextRand() * 8.f, 0.f};
      p.color = {0.55f, 0.65f, 0.85f, 0.55f};
      p.size = 1.5f + NextRand() * 2.f;
      p.life = 0.6f + NextRand() * 0.5f;
    }
    curtain_.push_back(p);
    curtain_accum_ -= 1.f;
  }
  if (curtain_.size() >= 128) {
    curtain_accum_ = 0.f;
  }
  if (rate < 0.5f) {
    curtain_.clear();
    curtain_accum_ = 0.f;
    return;
  }
  for (auto& p : curtain_) {
    p.life -= dt;
    p.position = p.position + p.velocity * dt;
  }
  curtain_.erase(std::remove_if(curtain_.begin(), curtain_.end(),
                                [](const CurtainParticle& p) { return p.life <= 0.f; }),
                 curtain_.end());
}

}  // namespace engine::render
