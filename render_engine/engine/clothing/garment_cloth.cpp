#include "engine/clothing/garment_cloth.h"

#include <algorithm>
#include <cmath>

namespace engine::clothing {
namespace {

bool IsFiniteVec(const Vec3& v) {
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void CollideParticleVsCapsule(Vec3& p, const CapsuleCollider& cap) {
  const float cyl_bottom = cap.center.y - cap.half_height;
  const float cyl_top = cap.center.y + cap.half_height;
  Vec3 center = cap.center;
  if (p.y < cyl_bottom) {
    center.y = cyl_bottom;
  } else if (p.y > cyl_top) {
    center.y = cyl_top;
  } else {
    center.y = p.y;
  }
  const Vec3 d = p - center;
  const float dist_sq = d.length_squared();
  const float r = cap.radius;
  if (dist_sq < 1e-12f) {
    p = center + Vec3{r, 0.f, 0.f};
    return;
  }
  if (dist_sq < r * r) {
    const float dist = std::sqrt(dist_sq);
    p = center + d * (r / dist);
  }
}

bool IsPinnedIndex(const std::vector<int>& pinned, int vi) {
  return std::find(pinned.begin(), pinned.end(), vi) != pinned.end();
}

}  // namespace

void GarmentCloth::Generate(const GarmentMeshDesc& desc, const Vec3& origin) {
  positions.clear();
  prev_positions.clear();
  indices.clear();
  pinned.clear();
  pin_targets.clear();
  soft_body_id = -1;

  grid_cols_ = std::max(2, desc.cols);
  grid_rows_ = std::max(2, desc.rows);
  cell_w_ = desc.width / static_cast<float>(grid_cols_ - 1);
  cell_h_ = desc.length / static_cast<float>(grid_rows_ - 1);

  positions.reserve(static_cast<std::size_t>(grid_rows_ * grid_cols_));

  for (int r = 0; r < grid_rows_; ++r) {
    const float v = static_cast<float>(r) / static_cast<float>(grid_rows_ - 1);
    for (int c = 0; c < grid_cols_; ++c) {
      const float u = static_cast<float>(c) / static_cast<float>(grid_cols_ - 1);
      Vec3 p;
      if (desc.kind == GarmentKind::Cape) {
        p = origin + Vec3{(u - 0.5f) * desc.width, -v * desc.length, -0.05f * v};
      } else {
        const float flare = 1.f + 0.35f * v;
        p = origin + Vec3{(u - 0.5f) * desc.width * flare, -v * desc.length,
                          (0.5f - u) * 0.02f};
      }
      positions.push_back(p);
    }
  }
  prev_positions = positions;

  for (int r = 0; r < grid_rows_ - 1; ++r) {
    for (int c = 0; c < grid_cols_ - 1; ++c) {
      const std::uint32_t i0 = static_cast<std::uint32_t>(r * grid_cols_ + c);
      const std::uint32_t i1 = static_cast<std::uint32_t>(r * grid_cols_ + c + 1);
      const std::uint32_t i2 = static_cast<std::uint32_t>((r + 1) * grid_cols_ + c);
      const std::uint32_t i3 = static_cast<std::uint32_t>((r + 1) * grid_cols_ + c + 1);
      indices.push_back(i0);
      indices.push_back(i2);
      indices.push_back(i1);
      indices.push_back(i1);
      indices.push_back(i2);
      indices.push_back(i3);
    }
  }

  pinned.clear();
  for (int c = 0; c < grid_cols_; ++c) {
    pinned.push_back(c);
  }
  pin_targets.assign(pinned.size(), origin);
}

void GarmentCloth::SetAttachPoints(const std::vector<Vec3>& world_points) {
  if (pinned.empty()) {
    return;
  }
  pin_targets.resize(pinned.size());
  if (world_points.empty()) {
    return;
  }
  if (world_points.size() == 1) {
    for (std::size_t i = 0; i < pin_targets.size(); ++i) {
      pin_targets[i] = world_points[0];
    }
    return;
  }
  for (std::size_t i = 0; i < pin_targets.size(); ++i) {
    const float t =
        static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(1, pin_targets.size() - 1));
    const float src = t * static_cast<float>(world_points.size() - 1);
    const int i0 = static_cast<int>(src);
    const int i1 = std::min(i0 + 1, static_cast<int>(world_points.size()) - 1);
    const float f = src - static_cast<float>(i0);
    const Vec3& a = world_points[static_cast<std::size_t>(i0)];
    const Vec3& b = world_points[static_cast<std::size_t>(i1)];
    pin_targets[i] = a + (b - a) * f;
  }
}

bool GarmentCloth::TryWirePhysicsSoftBody(physics::IPhysicsWorld& world, const Vec3& position) {
  physics::SoftBodyDesc desc;
  desc.position = position;
  desc.grid = 4;
  desc.cell = 0.15f;
  desc.mass = 1.f;
  soft_body_id = world.CreateSoftBody(desc);
  return soft_body_id >= 0;
}

bool GarmentCloth::SyncFromPhysics(physics::IPhysicsWorld& world) {
  if (soft_body_id < 0) {
    return false;
  }
  std::vector<Vec3> verts;
  if (!world.SoftBodyGetVertices(soft_body_id, verts) || verts.empty()) {
    return false;
  }
  positions = verts;
  prev_positions = verts;
  return true;
}

void GarmentCloth::ApplyPins() {
  for (std::size_t pi = 0; pi < pinned.size() && pi < pin_targets.size(); ++pi) {
    const int vi = pinned[pi];
    if (vi >= 0 && vi < static_cast<int>(positions.size())) {
      positions[static_cast<std::size_t>(vi)] = pin_targets[pi];
      prev_positions[static_cast<std::size_t>(vi)] = pin_targets[pi];
    }
  }
}

void GarmentCloth::Step(float dt, const CapsuleCollider* capsule) {
  if (positions.empty()) {
    return;
  }
  dt = std::clamp(dt, 0.f, 0.05f);
  if (prev_positions.size() != positions.size()) {
    prev_positions = positions;
  }

  for (std::size_t i = 0; i < positions.size(); ++i) {
    if (IsPinnedIndex(pinned, static_cast<int>(i))) {
      continue;
    }
    Vec3& p = positions[i];
    Vec3& prev = prev_positions[i];
    const Vec3 vel = (p - prev) * damping;
    prev = p;
    p = p + vel + Vec3{0.f, -gravity * dt * dt, 0.f};
  }

  ApplyPins();

  const int cols = grid_cols_;
  const int rows = grid_rows_;
  if (rows >= 2 && cols >= 2 &&
      static_cast<int>(positions.size()) == rows * cols) {
    auto idx = [cols](int r, int c) { return static_cast<std::size_t>(r * cols + c); };

    for (int iter = 0; iter < solver_iterations; ++iter) {
      ApplyPins();
      auto constrain = [&](int r0, int c0, int r1, int c1, float rest_len) {
        const std::size_t ia = idx(r0, c0);
        const std::size_t ib = idx(r1, c1);
        Vec3& a = positions[ia];
        Vec3& b = positions[ib];
        Vec3 delta = b - a;
        const float dist = delta.length();
        if (dist < 1e-8f) {
          return;
        }
        const float diff = (dist - rest_len) / dist;
        const Vec3 corr = delta * (0.5f * stretch_stiffness * diff);
        const bool pin_a = IsPinnedIndex(pinned, static_cast<int>(ia));
        const bool pin_b = IsPinnedIndex(pinned, static_cast<int>(ib));
        if (!pin_a && !pin_b) {
          a = a + corr;
          b = b - corr;
        } else if (pin_a && !pin_b) {
          b = b - corr * 2.f;
        } else if (!pin_a && pin_b) {
          a = a + corr * 2.f;
        }
      };

      for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
          if (c + 1 < cols) {
            constrain(r, c, r, c + 1, cell_w_);
          }
          if (r + 1 < rows) {
            constrain(r, c, r + 1, c, cell_h_);
          }
        }
      }
    }
  }

  ApplyPins();

  if (capsule) {
    for (std::size_t i = 0; i < positions.size(); ++i) {
      if (IsPinnedIndex(pinned, static_cast<int>(i))) {
        continue;
      }
      CollideParticleVsCapsule(positions[i], *capsule);
    }
  }
}

bool GarmentCloth::AllFinite() const {
  for (const Vec3& p : positions) {
    if (!IsFiniteVec(p)) {
      return false;
    }
  }
  return !positions.empty();
}

}  // namespace engine::clothing
