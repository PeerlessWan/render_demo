#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <string_view>

namespace mc {

enum class Id : std::uint8_t {
  Air = 0,
  Bedrock,
  Stone,
  Dirt,
  Grass,
  Sand,
  Gravel,
  OakLog,
  OakLeaves,
  OakPlanks,
  Cobble,
  CoalOre,
  IronOre,
  CraftingTable,
  Furnace,
  Chest,
  Glass,
  Water,
  Coal,
  IronIngot,
  Stick,
  WoodenPickaxe,
  StonePickaxe,
  WoodenAxe,
  StoneAxe,
  WoodenShovel,
  StoneShovel,
  WoodenSword,
  StoneSword,
  Bread,
  RawBeef,
  CookedBeef,
  Torch,
  Bed,
  RawPork,
  CookedPork,
  Count
};

struct Def {
  Id id = Id::Air;
  const char* name = "air";
  engine::ColorRgba color{1, 1, 1, 1};
  bool solid = false;
  bool block = false;
  bool transparent = false;
  float hardness = 0.f;  // seconds with bare hand; 0 = instant (air)
  Id drop = Id::Air;
  int drop_count = 1;
  int tool_tier = 0;  // 0 none, 1 wood, 2 stone
};

[[nodiscard]] const Def& GetDef(Id id);
[[nodiscard]] bool IsBlock(Id id);
[[nodiscard]] bool IsSolid(Id id);
[[nodiscard]] bool IsTransparent(Id id);
[[nodiscard]] int ToolTier(Id id);
[[nodiscard]] float BreakTime(Id block, Id tool);
[[nodiscard]] bool Falls(Id id);
[[nodiscard]] bool EmitsLight(Id id);
[[nodiscard]] int MaxDurability(Id id);
[[nodiscard]] bool IsTool(Id id);

}  // namespace mc
