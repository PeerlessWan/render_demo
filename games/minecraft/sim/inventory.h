#pragma once

#include "sim/blocks.h"

#include <cstdint>

namespace mc {

struct Stack {
  Id id = Id::Air;
  std::uint8_t count = 0;
  std::uint16_t wear = 0;
  [[nodiscard]] bool empty() const { return id == Id::Air || count == 0; }
};

class Inventory;

void ClickStacks(Stack* slot, Stack* cursor);
void WearStack(Stack* s, int amount = 1);
void FillCreativeInventory(Inventory* inv);

class Inventory {
 public:
  static constexpr int kSize = 36;
  static constexpr int kHotbar = 9;

  Stack slots[kSize]{};
  int selected = 0;
  Stack cursor{};

  bool Add(Id id, int n = 1);
  bool AddStack(Stack s);
  Stack RemoveSelected(int n = 1);
  [[nodiscard]] Stack Hotbar() const;
  void Cycle(int delta);
};

}  // namespace mc
