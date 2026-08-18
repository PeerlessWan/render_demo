#include "sim/mobs.h"

#include "sim/blocks.h"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

std::uint32_t H(int x, int z, int n) {
  std::uint32_t h = 2166136261u ^ static_cast<std::uint32_t>(n);
  h ^= static_cast<std::uint32_t>(x) * 16777619u;
  h ^= static_cast<std::uint32_t>(z) * 2246822519u;
  return h;
}

int SurfaceY(World& w, int x, int z) {
  for (int y = kChunkH - 2; y > 2; --y) {
    if (IsSolid(w.Get(x, y, z)) && !IsSolid(w.Get(x, y + 1, z))) {
      return y + 1;
    }
  }
  return 40;
}

void MoveToward(World& w, Mob* m, engine::Vec3 target, float speed, float dt) {
  engine::Vec3 d{target.x - m->pos.x, 0.f, target.z - m->pos.z};
  if (d.length_squared() < 1e-4f) {
    return;
  }
  d = engine::Normalize(d) * (speed * dt);
  engine::Vec3 n = m->pos + d;
  n.y = static_cast<float>(SurfaceY(w, static_cast<int>(std::floor(n.x)), static_cast<int>(std::floor(n.z))));
  if (!IsSolid(w.Get(static_cast<int>(std::floor(n.x)), static_cast<int>(n.y),
                     static_cast<int>(std::floor(n.z))))) {
    m->pos = n;
  }
}

}  // namespace

void TickMobs(World& world, Player* player, Clock clock, std::vector<Mob>* mobs, float dt) {
  if (!mobs || !player) {
    return;
  }
  int cows = 0;
  int zeds = 0;
  int pigs = 0;
  int sheep = 0;
  for (const auto& m : *mobs) {
    if (!m.alive) {
      continue;
    }
    if (m.kind == MobKind::Cow) {
      ++cows;
    } else if (m.kind == MobKind::Zombie) {
      ++zeds;
    } else if (m.kind == MobKind::Pig) {
      ++pigs;
    } else {
      ++sheep;
    }
  }
  const int px = static_cast<int>(std::floor(player->pos.x));
  const int pz = static_cast<int>(std::floor(player->pos.z));
  auto spawn = [&](MobKind kind, int count, int cap, float hp, int salt) {
    if (count >= cap) {
      return;
    }
    const int ox = px + static_cast<int>(H(px, pz, count + salt) % 17) - 8;
    const int oz = pz + static_cast<int>(H(pz, px, count + salt + 3) % 17) - 8;
    Mob m;
    m.kind = kind;
    m.hp = hp;
    m.pos = {ox + 0.5f, static_cast<float>(SurfaceY(world, ox, oz)), oz + 0.5f};
    mobs->push_back(m);
  };
  spawn(MobKind::Cow, cows, 4, 8.f, 1);
  spawn(MobKind::Pig, pigs, 3, 8.f, 5);
  spawn(MobKind::Sheep, sheep, 3, 6.f, 9);
  if (clock.Night()) {
    spawn(MobKind::Zombie, zeds, 8, 12.f, 13);
  }
  for (auto& m : *mobs) {
    if (!m.alive) {
      continue;
    }
    m.hurt_cd -= dt;
    if (m.kind == MobKind::Zombie) {
      if (!clock.Night()) {
        m.hp -= dt * 4.f;
        if (m.hp <= 0.f) {
          m.alive = false;
        }
      }
      MoveToward(world, &m, player->pos, 2.6f, dt);
      const engine::Vec3 d = m.pos - player->pos;
      if (d.length() < 1.4f && m.hurt_cd <= 0.f && m.alive) {
        Hurt(player, 3.f);
        m.hurt_cd = 1.2f;
      }
    } else {
      const float t = static_cast<float>(H(static_cast<int>(m.pos.x), static_cast<int>(m.pos.z), 1) % 100) /
                      50.f;
      MoveToward(world, &m, m.pos + engine::Vec3{t - 1.f, 0.f, 1.f - t}, 1.2f, dt);
    }
  }
  mobs->erase(std::remove_if(mobs->begin(), mobs->end(), [](const Mob& m) { return !m.alive; }),
              mobs->end());
}

bool HitMob(std::vector<Mob>* mobs, Player* player, engine::Vec3 origin, engine::Vec3 dir, Id* drop_id,
            engine::Vec3* drop_pos) {
  if (!mobs || !player) {
    return false;
  }
  if (drop_id) {
    *drop_id = Id::Air;
  }
  dir = engine::Normalize(dir);
  float best = 4.f;
  Mob* pick = nullptr;
  for (auto& m : *mobs) {
    if (!m.alive) {
      continue;
    }
    const engine::Vec3 rel = m.pos + engine::Vec3{0.f, 0.8f, 0.f} - origin;
    const float t = engine::Dot(rel, dir);
    if (t < 0.f || t > best) {
      continue;
    }
    const engine::Vec3 closest = origin + dir * t;
    const engine::Vec3 d = (m.pos + engine::Vec3{0.f, 0.8f, 0.f}) - closest;
    if (d.length() < 0.7f) {
      best = t;
      pick = &m;
    }
  }
  if (!pick) {
    return false;
  }
  float dmg = 1.f;
  const Id tool = player->inv.Hotbar().id;
  if (tool == Id::WoodenSword) {
    dmg = 4.f;
  } else if (tool == Id::StoneSword) {
    dmg = 6.f;
  }
  pick->hp -= dmg;
  if (pick->hp <= 0.f) {
    pick->alive = false;
    Id drop = Id::Air;
    if (pick->kind == MobKind::Cow) {
      drop = Id::RawBeef;
    } else if (pick->kind == MobKind::Pig) {
      drop = Id::RawPork;
    }
    if (drop_id) {
      *drop_id = drop;
    }
    if (drop_pos) {
      *drop_pos = pick->pos;
    }
  }
  return true;
}

}  // namespace mc
