#include "engine/physics/buoyancy.h"

#include <algorithm>
#include <cmath>

namespace engine::physics {
namespace {

Vec3 RotateQuat(const Quat& q, const Vec3& v) { return q.Rotate(v); }

}  // namespace

BuoyancyForces ComputeBoatBuoyancy(const ocean::FftOcean& ocean, const Vec3& boat_position,
                                   const Quat& boat_orientation, const Vec3* local_probes,
                                   std::size_t probe_count, const BoatBuoyancyParams& params,
                                   float dt, float& inout_flood_mass) {
  BuoyancyForces out;
  if (!local_probes || probe_count == 0) {
    return out;
  }
  const std::size_t count = std::min(probe_count, static_cast<std::size_t>(8));
  const float mass = std::max(1.f, params.mass + inout_flood_mass);
  float water_sum = 0.f;
  float submerged = 0.f;
  Vec3 force{};
  Vec3 torque{};

  for (std::size_t i = 0; i < count; ++i) {
    const Vec3 world_probe = boat_position + RotateQuat(boat_orientation, local_probes[i]);
    const float water_y = ocean.SampleHeight(world_probe.x, world_probe.z);
    water_sum += water_y;
    const float depth = water_y - world_probe.y;
    if (depth <= 0.f) {
      continue;
    }
    ++out.active_probes;
    submerged += 1.f;
    // Hydrostatic-ish upward force proportional to depth * area.
    const float lift = params.water_density * params.gravity * depth * params.probe_area *
                       params.buoyancy_factor / static_cast<float>(count);
    const Vec3 f{0.f, lift, 0.f};
    force = force + f;
    const Vec3 r = world_probe - boat_position;
    torque = torque + Cross(r, f);
  }

  out.submerged_fraction = submerged / static_cast<float>(count);
  out.average_water_height = water_sum / static_cast<float>(count);

  // Gravity + flood mass.
  force.y -= mass * params.gravity;

  // Linear drag opposing vertical motion is approximated as damping toward water plane.
  // Host may also apply horizontal drag; we damp excess upward/downward via submerged fraction.
  force.y -= params.linear_drag * out.submerged_fraction * 40.f *
             (boat_position.y - out.average_water_height);

  // Thrust along boat forward (+Z local).
  if (std::fabs(params.thrust) > 1e-4f) {
    const Vec3 forward = RotateQuat(boat_orientation, Vec3{0.f, 0.f, 1.f});
    force = force + forward * params.thrust;
  }

  // Angular drag (simple opposing torque scale).
  torque = torque * (1.f / (1.f + params.angular_drag * dt));

  // Flooding while submerged.
  if (params.flood_rate > 0.f && out.submerged_fraction > 0.15f) {
    inout_flood_mass = std::min(params.max_flood_mass,
                                inout_flood_mass + params.flood_rate * out.submerged_fraction * dt);
  }
  out.flood_mass = inout_flood_mass;
  out.force = force;
  out.torque = torque;
  return out;
}

BuoyancyForces ComputeBoatBuoyancy(const ocean::FftOcean& ocean, const Vec3& boat_position,
                                   const Quat& boat_orientation, const BoatBuoyancyParams& params,
                                   float dt, float& inout_flood_mass) {
  const auto probes = DefaultBoatProbes();
  return ComputeBoatBuoyancy(ocean, boat_position, boat_orientation, probes.data(), probes.size(),
                             params, dt, inout_flood_mass);
}

bool ApplyBuoyancyForces(IPhysicsWorld& world, int body_id, const BuoyancyForces& forces,
                         float dt) {
  if (body_id < 0 || body_id >= world.body_count() || dt <= 0.f) {
    return false;
  }
  const Vec3 impulse = forces.force * dt;
  return world.ApplyImpulse(body_id, impulse);
}

}  // namespace engine::physics
