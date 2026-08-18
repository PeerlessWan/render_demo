#include "sim/player.h"

#include "sim/blocks.h"

#include <cmath>

namespace mc {
namespace {

constexpr float kHalf = 0.3f;
constexpr float kHeight = 1.8f;

bool SolidAt(const World& w, int x, int y, int z) { return IsSolid(w.Get(x, y, z)); }

bool Overlaps(const World& w, engine::Vec3 p) {
  const int x0 = static_cast<int>(std::floor(p.x - kHalf));
  const int x1 = static_cast<int>(std::floor(p.x + kHalf));
  const int y0 = static_cast<int>(std::floor(p.y + 0.01f));
  const int y1 = static_cast<int>(std::floor(p.y + kHeight - 0.01f));
  const int z0 = static_cast<int>(std::floor(p.z - kHalf));
  const int z1 = static_cast<int>(std::floor(p.z + kHalf));
  for (int y = y0; y <= y1; ++y) {
    for (int z = z0; z <= z1; ++z) {
      for (int x = x0; x <= x1; ++x) {
        if (SolidAt(w, x, y, z)) {
          return true;
        }
      }
    }
  }
  return false;
}

void MoveAxis(World& w, engine::Vec3* p, float* vel, int axis, float dt) {
  engine::Vec3 n = *p;
  if (axis == 0) {
    n.x += *vel * dt;
  } else if (axis == 1) {
    n.y += *vel * dt;
  } else {
    n.z += *vel * dt;
  }
  if (Overlaps(w, n)) {
    *vel = 0.f;
  } else {
    *p = n;
  }
}

}  // namespace

void SpawnOnSurface(World& world, Player* p) {
  if (!p) {
    return;
  }
  world.StreamAround(0, 0, 4);
  auto try_at = [&](int x, int z) {
    for (int y = kChunkH - 3; y > 4; --y) {
      const Id below = world.Get(x, y, z);
      if (!IsSolid(below) || below == Id::OakLeaves || below == Id::OakLog || below == Id::Water) {
        continue;
      }
      if (world.Get(x, y + 1, z) != Id::Air || world.Get(x, y + 2, z) != Id::Air) {
        continue;
      }
      p->pos = {static_cast<float>(x) + 0.5f, static_cast<float>(y + 1), static_cast<float>(z) + 0.5f};
      p->vel = {};
      p->dead = false;
      p->hp = 20.f;
      if (!p->has_spawn) {
        p->spawn = p->pos;
        p->has_spawn = true;
      }
      return true;
    }
    return false;
  };
  if (try_at(0, 0)) {
    return;
  }
  for (int r = 1; r <= 8; ++r) {
    for (int x = -r; x <= r; ++x) {
      for (int z = -r; z <= r; ++z) {
        if (try_at(x, z)) {
          return;
        }
      }
    }
  }
  p->pos = {0.5f, 40.f, 0.5f};
}

void PlantStarterGrove(World& world, const engine::Vec3& at) {
  const int ox = static_cast<int>(std::floor(at.x));
  const int oz = static_cast<int>(std::floor(at.z));
  const int spots[4][2] = {{4, 3}, {-5, 4}, {6, -4}, {-3, -6}};
  for (const auto& s : spots) {
    const int x = ox + s[0];
    const int z = oz + s[1];
    int y = 8;
    for (int yy = kChunkH - 3; yy > 4; --yy) {
      if (IsSolid(world.Get(x, yy, z)) && world.Get(x, yy + 1, z) == Id::Air) {
        y = yy;
        break;
      }
    }
    const Id ground = world.Get(x, y, z);
    if (ground != Id::Grass && ground != Id::Dirt) {
      world.Set(x, y, z, Id::Grass);
    }
    for (int i = 1; i <= 4; ++i) {
      world.Set(x, y + i, z, Id::OakLog);
    }
    for (int dy = 3; dy <= 5; ++dy) {
      for (int dx = -2; dx <= 2; ++dx) {
        for (int dz = -2; dz <= 2; ++dz) {
          if (std::abs(dx) == 2 && std::abs(dz) == 2 && dy == 5) {
            continue;
          }
          const int lx = x + dx;
          const int ly = y + dy;
          const int lz = z + dz;
          if (world.Get(lx, ly, lz) == Id::Air) {
            world.Set(lx, ly, lz, Id::OakLeaves);
          }
        }
      }
    }
  }
}

void GiveSurvivalKit(Player* p) {
  if (!p || p->creative) {
    return;
  }
  (void)p->inv.Add(Id::WoodenAxe, 1);
  (void)p->inv.Add(Id::WoodenPickaxe, 1);
  (void)p->inv.Add(Id::Bread, 8);
}

engine::Vec3 Eye(const Player& p) { return p.pos + engine::Vec3{0.f, 1.62f, 0.f}; }

engine::Vec3 LookDir(const engine::render::Camera& cam) {
  return engine::Quat::FromEulerYxz(cam.yaw, cam.pitch, 0.f).Rotate(engine::Vec3{0.f, 0.f, -1.f});
}

void SyncCamera(Player& p, engine::render::Camera* cam) {
  if (!cam) {
    return;
  }
  cam->position = Eye(p);
}

void RespawnAtSpawn(World& world, Player* p) {
  if (!p) {
    return;
  }
  p->hp = 20.f;
  p->hunger = 20.f;
  p->dead = false;
  p->vel = {};
  if (p->has_spawn) {
    p->pos = p->spawn;
    return;
  }
  SpawnOnSurface(world, p);
}

void Hurt(Player* p, float dmg) {
  if (!p || p->creative || p->dead) {
    return;
  }
  p->hp -= dmg;
  if (p->hp <= 0.f) {
    p->hp = 0.f;
    p->dead = true;
    for (int i = 0; i < Inventory::kSize; ++i) {
      p->inv.slots[i] = {};
    }
  }
}

void TryEat(Player* p) {
  if (!p || p->hunger >= 20.f) {
    return;
  }
  const Id id = p->inv.Hotbar().id;
  float food = 0.f;
  if (id == Id::Bread) {
    food = 5.f;
  } else if (id == Id::CookedBeef) {
    food = 8.f;
  } else if (id == Id::RawBeef) {
    food = 3.f;
  } else if (id == Id::CookedPork) {
    food = 8.f;
  } else if (id == Id::RawPork) {
    food = 3.f;
  }
  if (food <= 0.f) {
    return;
  }
  (void)p->inv.RemoveSelected(1);
  p->hunger += food;
  if (p->hunger > 20.f) {
    p->hunger = 20.f;
  }
}

void TickPlayer(World& world, Player* p, const engine::render::Camera& cam, float dt, engine::Vec3 wish,
                bool jump, bool sneak) {
  if (!p || p->dead) {
    return;
  }
  const engine::Vec3 f = LookDir(cam);
  engine::Vec3 forward{f.x, 0.f, f.z};
  if (forward.length_squared() > 1e-6f) {
    forward = engine::Normalize(forward);
  }
  const engine::Vec3 right = engine::Normalize(engine::Vec3{-forward.z, 0.f, forward.x});
  engine::Vec3 move = forward * wish.z + right * wish.x;
  if (p->creative && p->flying) {
    move.y = wish.y;
    if (move.length_squared() > 1e-6f) {
      move = engine::Normalize(move) * (sneak ? 6.f : 12.f);
    }
    p->vel = move;
    p->pos += p->vel * dt;
    if (Overlaps(world, p->pos)) {
      p->pos.y += 0.2f;
    }
    p->on_ground = false;
    return;
  }
  const float speed = sneak ? 2.2f : 4.3f;
  if (move.length_squared() > 1e-6f) {
    move = engine::Normalize(move) * speed;
  }
  p->vel.x = move.x;
  p->vel.z = move.z;
  p->vel.y -= 28.f * dt;
  if (p->vel.y < -40.f) {
    p->vel.y = -40.f;
  }
  const float before_y = p->vel.y;
  MoveAxis(world, &p->pos, &p->vel.x, 0, dt);
  MoveAxis(world, &p->pos, &p->vel.z, 2, dt);
  MoveAxis(world, &p->pos, &p->vel.y, 1, dt);
  p->on_ground = before_y < 0.f && p->vel.y == 0.f;
  if (p->on_ground && jump) {
    p->vel.y = 8.4f;
    p->on_ground = false;
  }
  if (p->pos.y < -4.f) {
    Hurt(p, 20.f);
  }
}

bool TryBreak(World& world, Player* p, const TraceHit& hit, float dt) {
  if (!p || !hit.hit) {
    p->breaking = false;
    p->break_acc = 0.f;
    return false;
  }
  const Id id = world.Get(hit.x, hit.y, hit.z);
  if (id == Id::Bedrock || !IsBlock(id) || id == Id::Water) {
    return false;
  }
  if (p->creative) {
    world.Set(hit.x, hit.y, hit.z, Id::Air);
    p->breaking = false;
    return true;
  }
  if (!p->breaking || p->break_x != hit.x || p->break_y != hit.y || p->break_z != hit.z) {
    p->breaking = true;
    p->break_acc = 0.f;
    p->break_x = hit.x;
    p->break_y = hit.y;
    p->break_z = hit.z;
  }
  p->break_acc += dt;
  if (p->break_acc >= BreakTime(id, p->inv.Hotbar().id)) {
    world.Set(hit.x, hit.y, hit.z, Id::Air);
    WearStack(&p->inv.slots[p->inv.selected], 1);
    p->breaking = false;
    p->break_acc = 0.f;
    return true;
  }
  return false;
}

bool TryPlace(World& world, Player* p, const TraceHit& hit) {
  if (!p || !hit.hit) {
    return false;
  }
  Stack held = p->inv.Hotbar();
  if (held.empty() || (!IsBlock(held.id) && held.id != Id::Water)) {
    return false;
  }
  const int x = hit.px;
  const int y = hit.py;
  const int z = hit.pz;
  if (world.Get(x, y, z) != Id::Air && world.Get(x, y, z) != Id::Water) {
    return false;
  }
  // Do not place inside player.
  const float dx = (static_cast<float>(x) + 0.5f) - p->pos.x;
  const float dz = (static_cast<float>(z) + 0.5f) - p->pos.z;
  const float dy = static_cast<float>(y) - p->pos.y;
  if (std::fabs(dx) < 0.8f && std::fabs(dz) < 0.8f && dy >= -0.2f && dy < kHeight) {
    return false;
  }
  world.Set(x, y, z, held.id);
  if (!p->creative) {
    (void)p->inv.RemoveSelected(1);
  }
  return true;
}

}  // namespace mc
