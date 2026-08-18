#pragma once

#include "sim/player.h"
#include "sim/time.h"
#include "world/world.h"

#include "engine/core/math.h"

#include <vector>

namespace mc {

enum class MobKind { Cow, Zombie, Pig, Sheep };

struct Mob {
  MobKind kind = MobKind::Cow;
  engine::Vec3 pos{};
  float hp = 10.f;
  float hurt_cd = 0.f;
  bool alive = true;
};

void TickMobs(World& world, Player* player, Clock clock, std::vector<Mob>* mobs, float dt);
bool HitMob(std::vector<Mob>* mobs, Player* player, engine::Vec3 origin, engine::Vec3 dir, Id* drop_id,
            engine::Vec3* drop_pos);

}  // namespace mc
