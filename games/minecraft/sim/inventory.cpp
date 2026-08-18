#include "sim/inventory.h"

#include <algorithm>
#include <utility>

namespace mc {

void ClickStacks(Stack* slot, Stack* cursor) {
  if (!slot || !cursor) {
    return;
  }
  if (cursor->empty()) {
    *cursor = *slot;
    *slot = {};
    return;
  }
  if (slot->empty()) {
    *slot = *cursor;
    *cursor = {};
    return;
  }
  if (slot->id == cursor->id && MaxDurability(slot->id) == 0) {
    const int room = 64 - slot->count;
    const int take = std::min(room, static_cast<int>(cursor->count));
    slot->count = static_cast<std::uint8_t>(slot->count + take);
    cursor->count = static_cast<std::uint8_t>(cursor->count - take);
    if (cursor->count == 0) {
      *cursor = {};
    }
    return;
  }
  std::swap(*slot, *cursor);
}

void WearStack(Stack* s, int amount) {
  if (!s || s->empty()) {
    return;
  }
  const int max_d = MaxDurability(s->id);
  if (max_d <= 0) {
    return;
  }
  const int next = static_cast<int>(s->wear) + amount;
  if (next >= max_d) {
    *s = {};
    return;
  }
  s->wear = static_cast<std::uint16_t>(next);
}

void FillCreativeInventory(Inventory* inv) {
  if (!inv) {
    return;
  }
  const Id blocks[] = {Id::Stone,         Id::Dirt,          Id::Grass,     Id::Sand,     Id::Gravel,
                       Id::OakLog,        Id::OakLeaves,     Id::OakPlanks, Id::Cobble,   Id::CoalOre,
                       Id::IronOre,       Id::CraftingTable, Id::Furnace,   Id::Chest,    Id::Glass,
                       Id::Water,         Id::Torch,         Id::Bed,       Id::Coal,     Id::Stick,
                       Id::WoodenPickaxe, Id::StonePickaxe,  Id::WoodenAxe, Id::StoneAxe, Id::WoodenShovel,
                       Id::StoneShovel,   Id::WoodenSword,   Id::StoneSword, Id::Bread,   Id::CookedBeef};
  for (int i = 0; i < Inventory::kSize; ++i) {
    inv->slots[i] = {};
  }
  const int n = static_cast<int>(sizeof(blocks) / sizeof(blocks[0]));
  for (int i = 0; i < n && i < Inventory::kSize; ++i) {
    inv->slots[i].id = blocks[i];
    inv->slots[i].count = IsTool(blocks[i]) ? 1 : 64;
    inv->slots[i].wear = 0;
  }
}

bool Inventory::Add(Id id, int n) {
  Stack s;
  s.id = id;
  s.count = static_cast<std::uint8_t>(n < 64 ? n : 64);
  return AddStack(s) && (n <= 64);
}

bool Inventory::AddStack(Stack s) {
  if (s.empty()) {
    return false;
  }
  int n = s.count;
  if (MaxDurability(s.id) > 0) {
    for (int i = 0; i < kSize && n > 0; ++i) {
      if (slots[i].empty()) {
        slots[i] = s;
        slots[i].count = 1;
        --n;
      }
    }
    return n == 0;
  }
  for (int i = 0; i < kSize && n > 0; ++i) {
    if (slots[i].id == s.id && slots[i].count < 64) {
      const int room = 64 - slots[i].count;
      const int take = n < room ? n : room;
      slots[i].count = static_cast<std::uint8_t>(slots[i].count + take);
      n -= take;
    }
  }
  for (int i = 0; i < kSize && n > 0; ++i) {
    if (slots[i].empty()) {
      const int take = n < 64 ? n : 64;
      slots[i].id = s.id;
      slots[i].count = static_cast<std::uint8_t>(take);
      slots[i].wear = s.wear;
      n -= take;
    }
  }
  return n == 0;
}

Stack Inventory::RemoveSelected(int n) {
  Stack& s = slots[selected];
  if (s.empty()) {
    return {};
  }
  const int take = n < s.count ? n : s.count;
  Stack out = s;
  out.count = static_cast<std::uint8_t>(take);
  s.count = static_cast<std::uint8_t>(s.count - take);
  if (s.count == 0) {
    s = {};
  }
  return out;
}

Stack Inventory::Hotbar() const { return slots[selected]; }

void Inventory::Cycle(int delta) {
  selected = (selected + delta) % kHotbar;
  if (selected < 0) {
    selected += kHotbar;
  }
}

}  // namespace mc
