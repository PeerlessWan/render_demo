#pragma once

#include "sim/inventory.h"
#include "world/trace.h"
#include "world/world.h"

#include "engine/core/math.h"
#include "engine/render/camera.h"

namespace mc {

struct Player {
  engine::Vec3 pos{0.5f, 40.f, 0.5f};
  engine::Vec3 vel{};
  float hp = 20.f;
  float hunger = 20.f;
  bool creative = false;
  bool flying = false;
  bool on_ground = false;
  bool dead = false;
  Inventory inv;
  Stack craft_grid[9]{};
  engine::Vec3 spawn{0.5f, 40.f, 0.5f};
  bool has_spawn = false;
  bool ui_open = false;
  enum class Ui { None, Inventory, Table, Chest, Furnace } ui = Ui::None;
  int ui_x = 0;
  int ui_y = 0;
  int ui_z = 0;
  float break_acc = 0.f;
  int break_x = 0;
  int break_y = 0;
  int break_z = 0;
  bool breaking = false;
};

void SpawnOnSurface(World& world, Player* p);
void PlantStarterGrove(World& world, const engine::Vec3& at);
void GiveSurvivalKit(Player* p);
void TickPlayer(World& world, Player* p, const engine::render::Camera& cam, float dt, engine::Vec3 wish,
                bool jump, bool sneak);
void SyncCamera(Player& p, engine::render::Camera* cam);
[[nodiscard]] engine::Vec3 Eye(const Player& p);
[[nodiscard]] engine::Vec3 LookDir(const engine::render::Camera& cam);
bool TryBreak(World& world, Player* p, const TraceHit& hit, float dt);
bool TryPlace(World& world, Player* p, const TraceHit& hit);
void Hurt(Player* p, float dmg);
void TryEat(Player* p);
void RespawnAtSpawn(World& world, Player* p);

}  // namespace mc
