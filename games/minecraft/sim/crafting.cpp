#include "sim/crafting.h"

#include <array>

namespace mc {
namespace {

bool MatchShapeless(const Stack grid[9], const Id* need, int nneed) {
  int have[static_cast<int>(Id::Count)]{};
  int want[static_cast<int>(Id::Count)]{};
  int filled = 0;
  for (int i = 0; i < 9; ++i) {
    if (!grid[i].empty()) {
      have[static_cast<int>(grid[i].id)] += 1;
      ++filled;
    }
  }
  for (int i = 0; i < nneed; ++i) {
    want[static_cast<int>(need[i])] += 1;
  }
  if (filled != nneed) {
    return false;
  }
  for (int i = 0; i < static_cast<int>(Id::Count); ++i) {
    if (have[i] != want[i]) {
      return false;
    }
  }
  return true;
}

bool MatchShaped(const Stack grid[9], const Id pat[9]) {
  for (int i = 0; i < 9; ++i) {
    const Id g = grid[i].empty() ? Id::Air : grid[i].id;
    if (g != pat[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool Craft(Stack grid[9], bool table_3x3, Stack* result) {
  if (!result) {
    return false;
  }
  *result = {};
  const Id four_planks[] = {Id::OakLog};
  if (MatchShapeless(grid, four_planks, 1)) {
    *result = {Id::OakPlanks, 4};
    return true;
  }
  const Id sticks[] = {Id::OakPlanks, Id::OakPlanks};
  if (MatchShapeless(grid, sticks, 2)) {
    *result = {Id::Stick, 4};
    return true;
  }
  const Id torch[] = {Id::Coal, Id::Stick};
  if (MatchShapeless(grid, torch, 2)) {
    *result = {Id::Torch, 4};
    return true;
  }
  const Id table[] = {Id::OakPlanks, Id::OakPlanks, Id::OakPlanks, Id::OakPlanks};
  if (MatchShapeless(grid, table, 4)) {
    *result = {Id::CraftingTable, 1};
    return true;
  }
  const Id bread[] = {Id::OakPlanks, Id::OakPlanks, Id::OakPlanks};
  if (MatchShapeless(grid, bread, 3)) {
    *result = {Id::Bread, 1};
    return true;
  }
  if (!table_3x3) {
    return false;
  }
  const Id furnace[] = {Id::Cobble, Id::Cobble, Id::Cobble, Id::Cobble, Id::Cobble, Id::Cobble,
                        Id::Cobble, Id::Cobble};
  if (MatchShapeless(grid, furnace, 8)) {
    *result = {Id::Furnace, 1};
    return true;
  }
  const Id chest[] = {Id::OakPlanks, Id::OakPlanks, Id::OakPlanks, Id::OakPlanks, Id::OakPlanks,
                      Id::OakPlanks, Id::OakPlanks, Id::OakPlanks};
  if (MatchShapeless(grid, chest, 8)) {
    *result = {Id::Chest, 1};
    return true;
  }
  auto tool = [&](Id head, Id out) {
    Id pat[9] = {head, head, head, Id::Air, Id::Stick, Id::Air, Id::Air, Id::Stick, Id::Air};
    return MatchShaped(grid, pat) ? out : Id::Air;
  };
  if (auto o = tool(Id::OakPlanks, Id::WoodenPickaxe); o != Id::Air) {
    *result = {o, 1};
    return true;
  }
  if (auto o = tool(Id::Cobble, Id::StonePickaxe); o != Id::Air) {
    *result = {o, 1};
    return true;
  }
  Id axe_w[9] = {Id::OakPlanks, Id::OakPlanks, Id::Air, Id::OakPlanks, Id::Stick, Id::Air,
                 Id::Air,       Id::Stick,     Id::Air};
  if (MatchShaped(grid, axe_w)) {
    *result = {Id::WoodenAxe, 1};
    return true;
  }
  Id axe_s[9] = {Id::Cobble, Id::Cobble, Id::Air, Id::Cobble, Id::Stick, Id::Air,
                 Id::Air,    Id::Stick,  Id::Air};
  if (MatchShaped(grid, axe_s)) {
    *result = {Id::StoneAxe, 1};
    return true;
  }
  Id sh_w[9] = {Id::Air, Id::OakPlanks, Id::Air, Id::Air, Id::Stick, Id::Air, Id::Air, Id::Stick, Id::Air};
  if (MatchShaped(grid, sh_w)) {
    *result = {Id::WoodenShovel, 1};
    return true;
  }
  Id sh_s[9] = {Id::Air, Id::Cobble, Id::Air, Id::Air, Id::Stick, Id::Air, Id::Air, Id::Stick, Id::Air};
  if (MatchShaped(grid, sh_s)) {
    *result = {Id::StoneShovel, 1};
    return true;
  }
  Id sw_w[9] = {Id::Air, Id::OakPlanks, Id::Air, Id::Air, Id::OakPlanks, Id::Air, Id::Air, Id::Stick, Id::Air};
  if (MatchShaped(grid, sw_w)) {
    *result = {Id::WoodenSword, 1};
    return true;
  }
  Id sw_s[9] = {Id::Air, Id::Cobble, Id::Air, Id::Air, Id::Cobble, Id::Air, Id::Air, Id::Stick, Id::Air};
  if (MatchShaped(grid, sw_s)) {
    *result = {Id::StoneSword, 1};
    return true;
  }
  Id bed[9] = {Id::Air, Id::Air, Id::Air, Id::OakPlanks, Id::OakPlanks, Id::OakPlanks,
               Id::OakPlanks, Id::OakPlanks, Id::OakPlanks};
  if (MatchShaped(grid, bed)) {
    *result = {Id::Bed, 1};
    return true;
  }
  return false;
}

void ConsumeCraft(Stack grid[9], bool /*table_3x3*/) {
  for (int i = 0; i < 9; ++i) {
    if (!grid[i].empty()) {
      grid[i].count--;
      if (grid[i].count == 0) {
        grid[i].id = Id::Air;
      }
    }
  }
}

}  // namespace mc
