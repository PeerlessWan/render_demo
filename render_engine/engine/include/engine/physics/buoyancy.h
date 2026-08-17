#pragma once

#include "engine/core/math.h"
#include "engine/ocean/fft_ocean.h"
#include "engine/physics/i_physics_world.h"

#include <array>
#include <cstddef>

namespace engine::physics {

// Mega-W8: boat buoyancy from ocean height probes (not full vehicle sim).
struct BoatBuoyancyParams {
  float mass = 200.f;
  float buoyancy_factor = 1.35f;  // >1 floats at rest when fully "supported"
  float linear_drag = 1.8f;
  float angular_drag = 2.5f;
  float gravity = 9.81f;
  float water_density = 1000.f;
  float probe_area = 0.35f;  // m^2 per probe contribution
  float thrust = 0.f;        // forward force (local +Z), demo
  float flood_rate = 0.f;    // mass increase per second when submerged
  float max_flood_mass = 400.f;
};

struct BuoyancyForces {
  Vec3 force{};
  Vec3 torque{};
  float submerged_fraction = 0.f;
  float flood_mass = 0.f;
  float average_water_height = 0.f;
  int active_probes = 0;
};

// Local-space probe offsets on the boat hull (3–5 typical).
using BoatProbes = std::array<Vec3, 5>;

[[nodiscard]] inline BoatProbes DefaultBoatProbes(float half_len = 1.2f, float half_beam = 0.55f) {
  return BoatProbes{
      Vec3{0.f, 0.f, 0.f},
      Vec3{half_beam, 0.f, half_len * 0.55f},
      Vec3{-half_beam, 0.f, half_len * 0.55f},
      Vec3{half_beam * 0.6f, 0.f, -half_len * 0.7f},
      Vec3{-half_beam * 0.6f, 0.f, -half_len * 0.7f},
  };
}

// Sample ocean heights at world-space probes and accumulate force/torque.
[[nodiscard]] BuoyancyForces ComputeBoatBuoyancy(const ocean::FftOcean& ocean,
                                                 const Vec3& boat_position,
                                                 const Quat& boat_orientation,
                                                 const Vec3* local_probes, std::size_t probe_count,
                                                 const BoatBuoyancyParams& params, float dt,
                                                 float& inout_flood_mass);

// Convenience overload with DefaultBoatProbes.
[[nodiscard]] BuoyancyForces ComputeBoatBuoyancy(const ocean::FftOcean& ocean,
                                                 const Vec3& boat_position,
                                                 const Quat& boat_orientation,
                                                 const BoatBuoyancyParams& params, float dt,
                                                 float& inout_flood_mass);

// Apply computed forces to a dynamic body when the world supports impulses.
// Returns false if body missing or backend ignores impulses (caller may integrate manually).
bool ApplyBuoyancyForces(IPhysicsWorld& world, int body_id, const BuoyancyForces& forces, float dt);

}  // namespace engine::physics
