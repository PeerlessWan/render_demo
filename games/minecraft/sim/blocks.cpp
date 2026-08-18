#include "sim/blocks.h"

#include <array>

namespace mc {
namespace {

constexpr Def kDefs[] = {
    {Id::Air, "air", {0, 0, 0, 0}, false, false, true, 0.f, Id::Air, 0, 0},
    {Id::Bedrock, "bedrock", {0.22f, 0.22f, 0.24f, 1}, true, true, false, 1e6f, Id::Air, 0, 0},
    {Id::Stone, "stone", {0.45f, 0.45f, 0.48f, 1}, true, true, false, 1.5f, Id::Cobble, 1, 1},
    {Id::Dirt, "dirt", {0.45f, 0.32f, 0.18f, 1}, true, true, false, 0.5f, Id::Dirt, 1, 0},
    {Id::Grass, "grass", {0.35f, 0.62f, 0.22f, 1}, true, true, false, 0.6f, Id::Dirt, 1, 0},
    {Id::Sand, "sand", {0.86f, 0.80f, 0.52f, 1}, true, true, false, 0.5f, Id::Sand, 1, 0},
    {Id::Gravel, "gravel", {0.55f, 0.52f, 0.48f, 1}, true, true, false, 0.6f, Id::Gravel, 1, 0},
    {Id::OakLog, "oak_log", {0.42f, 0.28f, 0.14f, 1}, true, true, false, 2.f, Id::OakLog, 1, 0},
    {Id::OakLeaves, "oak_leaves", {0.22f, 0.48f, 0.18f, 1}, true, true, true, 0.2f, Id::Air, 0, 0},
    {Id::OakPlanks, "oak_planks", {0.72f, 0.58f, 0.32f, 1}, true, true, false, 2.f, Id::OakPlanks, 1, 0},
    {Id::Cobble, "cobble", {0.40f, 0.40f, 0.42f, 1}, true, true, false, 2.f, Id::Cobble, 1, 1},
    {Id::CoalOre, "coal_ore", {0.28f, 0.28f, 0.30f, 1}, true, true, false, 3.f, Id::Coal, 1, 1},
    {Id::IronOre, "iron_ore", {0.62f, 0.48f, 0.42f, 1}, true, true, false, 3.f, Id::IronOre, 1, 2},
    {Id::CraftingTable, "crafting_table", {0.62f, 0.42f, 0.18f, 1}, true, true, false, 2.5f, Id::CraftingTable, 1, 0},
    {Id::Furnace, "furnace", {0.38f, 0.38f, 0.40f, 1}, true, true, false, 3.5f, Id::Furnace, 1, 1},
    {Id::Chest, "chest", {0.58f, 0.38f, 0.12f, 1}, true, true, false, 2.5f, Id::Chest, 1, 0},
    {Id::Glass, "glass", {0.70f, 0.85f, 0.92f, 0.55f}, true, true, true, 0.3f, Id::Air, 0, 0},
    {Id::Water, "water", {0.18f, 0.38f, 0.78f, 0.45f}, false, true, true, 0.f, Id::Air, 0, 0},
    {Id::Coal, "coal", {0.12f, 0.12f, 0.14f, 1}, false, false, false, 0.f, Id::Coal, 1, 0},
    {Id::IronIngot, "iron_ingot", {0.78f, 0.80f, 0.86f, 1}, false, false, false, 0.f, Id::IronIngot, 1, 0},
    {Id::Stick, "stick", {0.55f, 0.38f, 0.18f, 1}, false, false, false, 0.f, Id::Stick, 1, 0},
    {Id::WoodenPickaxe, "wooden_pickaxe", {0.60f, 0.42f, 0.22f, 1}, false, false, false, 0.f, Id::WoodenPickaxe, 1, 1},
    {Id::StonePickaxe, "stone_pickaxe", {0.50f, 0.50f, 0.52f, 1}, false, false, false, 0.f, Id::StonePickaxe, 1, 2},
    {Id::WoodenAxe, "wooden_axe", {0.58f, 0.40f, 0.20f, 1}, false, false, false, 0.f, Id::WoodenAxe, 1, 1},
    {Id::StoneAxe, "stone_axe", {0.48f, 0.48f, 0.50f, 1}, false, false, false, 0.f, Id::StoneAxe, 1, 2},
    {Id::WoodenShovel, "wooden_shovel", {0.62f, 0.44f, 0.24f, 1}, false, false, false, 0.f, Id::WoodenShovel, 1, 1},
    {Id::StoneShovel, "stone_shovel", {0.52f, 0.52f, 0.54f, 1}, false, false, false, 0.f, Id::StoneShovel, 1, 2},
    {Id::WoodenSword, "wooden_sword", {0.64f, 0.46f, 0.26f, 1}, false, false, false, 0.f, Id::WoodenSword, 1, 1},
    {Id::StoneSword, "stone_sword", {0.54f, 0.54f, 0.56f, 1}, false, false, false, 0.f, Id::StoneSword, 1, 2},
    {Id::Bread, "bread", {0.72f, 0.52f, 0.28f, 1}, false, false, false, 0.f, Id::Bread, 1, 0},
    {Id::RawBeef, "raw_beef", {0.72f, 0.28f, 0.28f, 1}, false, false, false, 0.f, Id::RawBeef, 1, 0},
    {Id::CookedBeef, "cooked_beef", {0.48f, 0.28f, 0.16f, 1}, false, false, false, 0.f, Id::CookedBeef, 1, 0},
    {Id::Torch, "torch", {1.f, 0.82f, 0.28f, 1}, false, true, true, 0.1f, Id::Torch, 1, 0},
    {Id::Bed, "bed", {0.72f, 0.18f, 0.18f, 1}, true, true, false, 0.2f, Id::Bed, 1, 0},
    {Id::RawPork, "raw_pork", {0.78f, 0.42f, 0.42f, 1}, false, false, false, 0.f, Id::RawPork, 1, 0},
    {Id::CookedPork, "cooked_pork", {0.62f, 0.38f, 0.22f, 1}, false, false, false, 0.f, Id::CookedPork, 1, 0},
};

}  // namespace

const Def& GetDef(Id id) {
  const auto i = static_cast<std::size_t>(id);
  if (i >= static_cast<std::size_t>(Id::Count)) {
    return kDefs[0];
  }
  return kDefs[i];
}

bool IsBlock(Id id) { return GetDef(id).block; }
bool IsSolid(Id id) { return GetDef(id).solid; }
bool IsTransparent(Id id) { return GetDef(id).transparent; }

int ToolTier(Id id) { return GetDef(id).tool_tier; }

float BreakTime(Id block, Id tool) {
  const auto& b = GetDef(block);
  if (!b.block || b.hardness >= 1e5f) {
    return b.hardness;
  }
  float t = b.hardness;
  const int need = b.tool_tier;
  const int have = ToolTier(tool);
  if (need > 0 && have < need) {
    t *= 3.5f;
  } else if (have >= 2) {
    t *= 0.45f;
  } else if (have >= 1) {
    t *= 0.7f;
  }
  return t;
}

bool Falls(Id id) { return id == Id::Sand || id == Id::Gravel; }

bool EmitsLight(Id id) { return id == Id::Torch; }

bool IsTool(Id id) {
  switch (id) {
    case Id::WoodenPickaxe:
    case Id::StonePickaxe:
    case Id::WoodenAxe:
    case Id::StoneAxe:
    case Id::WoodenShovel:
    case Id::StoneShovel:
    case Id::WoodenSword:
    case Id::StoneSword:
      return true;
    default:
      return false;
  }
}

int MaxDurability(Id id) {
  switch (id) {
    case Id::WoodenPickaxe:
    case Id::WoodenAxe:
    case Id::WoodenShovel:
    case Id::WoodenSword:
      return 60;
    case Id::StonePickaxe:
    case Id::StoneAxe:
    case Id::StoneShovel:
    case Id::StoneSword:
      return 132;
    default:
      return 0;
  }
}

}  // namespace mc
