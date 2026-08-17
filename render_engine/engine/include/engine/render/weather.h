#pragma once

#include "engine/core/math.h"
#include "engine/render/atmosphere.h"
#include "engine/vfx/particles.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine::render {

// Mega-W8 / C05: weather state machine + zone sample (not meteorological simulation).
enum class WeatherState : std::uint8_t {
  Clear = 0,
  Cloudy,
  Rain,
  Storm,
  Snow,
};

struct WeatherZoneSample {
  WeatherState state = WeatherState::Clear;
  float intensity = 0.f;       // [0,1]
  float wind_speed = 0.f;      // m/s-ish demo scale
  float moisture = 0.f;        // wetness [0,1]
  float snow_cover = 0.f;      // accumulated cover [0,1]
  float lightning_flash = 0.f; // instantaneous flash [0,1]
  float precip_rate = 0.f;     // particles / sec scale
};

class WeatherSystem {
 public:
  void SetState(WeatherState state, float intensity = 1.f);
  void set_wind(const Vec3& dir, float speed);
  void set_transition_speed(float per_sec) { transition_speed_ = std::max(0.05f, per_sec); }

  void Update(float dt);

  [[nodiscard]] WeatherState state() const { return state_; }
  [[nodiscard]] float intensity() const { return intensity_; }
  [[nodiscard]] float snow_cover() const { return snow_cover_; }
  [[nodiscard]] float lightning_flash() const { return lightning_flash_; }
  [[nodiscard]] float moisture() const { return moisture_; }
  [[nodiscard]] Vec3 wind_dir() const { return wind_dir_; }
  [[nodiscard]] float wind_speed() const { return wind_speed_; }

  // Spatial sample (cheap hash modulation; same global state + local variation).
  [[nodiscard]] WeatherZoneSample SampleZone(const Vec3& world_pos) const;

  // Couple fog density / tint with atmosphere + current weather.
  [[nodiscard]] CoupledFog CoupleFog(const AtmosphereParams& atmosphere, const Vec3& view_dir,
                                     float base_fog_density, bool enable_clouds) const;

  // Drive existing VFX emitter as rain/snow curtain (caller Steps the emitter).
  void ConfigurePrecipEmitter(vfx::ParticleEmitter& emitter, const Vec3& camera_pos) const;

  // Simple CPU curtain particles (independent of ParticleEmitter).
  struct CurtainParticle {
    Vec3 position{};
    Vec3 velocity{};
    float life = 1.f;
    float size = 2.f;
    ColorRgba color{0.7f, 0.8f, 1.f, 0.7f};
  };
  void UpdateCurtain(float dt, const Vec3& camera_pos);
  [[nodiscard]] const std::vector<CurtainParticle>& curtain() const { return curtain_; }

 private:
  WeatherState state_ = WeatherState::Clear;
  WeatherState target_state_ = WeatherState::Clear;
  float intensity_ = 0.f;
  float target_intensity_ = 0.f;
  float transition_speed_ = 1.2f;
  float moisture_ = 0.f;
  float snow_cover_ = 0.f;
  float lightning_flash_ = 0.f;
  float storm_timer_ = 0.f;
  float time_ = 0.f;
  float curtain_accum_ = 0.f;
  Vec3 wind_dir_{1.f, 0.f, 0.2f};
  float wind_speed_ = 2.f;
  std::uint32_t rng_ = 1u;
  std::vector<CurtainParticle> curtain_;

  float NextRand();
  [[nodiscard]] float PrecipRate() const;
};

}  // namespace engine::render
