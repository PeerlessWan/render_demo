#pragma once

#include "sim/blocks.h"
#include "sim/containers.h"
#include "sim/mobs.h"
#include "sim/player.h"
#include "sim/time.h"
#include "world/world.h"

#include "engine/core/result.h"

#include <filesystem>
#include <vector>

namespace mc {

struct Drop {
  engine::Vec3 pos{};
  Id id = Id::Air;
  std::uint8_t count = 1;
  float life = 20.f;
};

struct GameState {
  World world{1};
  Player player;
  Clock clock;
  Containers boxes;
  std::vector<Mob> mobs;
  std::vector<Drop> drops;
  int view_radius = 4;
};

engine::Status SaveWorld(const GameState& st, const std::filesystem::path& dir);
engine::Status LoadWorld(GameState* st, const std::filesystem::path& dir);

}  // namespace mc
