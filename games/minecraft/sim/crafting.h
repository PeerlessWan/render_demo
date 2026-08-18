#pragma once

#include "sim/inventory.h"

namespace mc {

struct Recipe {
  Id a[9]{};
  Id out = Id::Air;
  int out_n = 1;
  bool shapeless = true;
};

bool Craft(Stack grid[9], bool table_3x3, Stack* result);
void ConsumeCraft(Stack grid[9], bool table_3x3);

}  // namespace mc
