#include "sim/containers.h"

#include "sim/blocks.h"

namespace mc {

ChestData& Containers::ChestAt(int x, int y, int z) { return chests[BlockPos{x, y, z}]; }

FurnaceData& Containers::FurnaceAt(int x, int y, int z) { return furnaces[BlockPos{x, y, z}]; }

void Containers::Remove(int x, int y, int z) {
  const BlockPos p{x, y, z};
  chests.erase(p);
  furnaces.erase(p);
}

void Containers::Tick(float dt) {
  for (auto& [pos, f] : furnaces) {
    (void)pos;
    auto fuel_value = [](Id id) {
      if (id == Id::Coal) {
        return 8.f;
      }
      if (id == Id::OakLog || id == Id::OakPlanks || id == Id::Stick) {
        return 1.5f;
      }
      return 0.f;
    };
    auto cook_out = [](Id id) -> Id {
      if (id == Id::Cobble) {
        return Id::Stone;
      }
      if (id == Id::Sand) {
        return Id::Glass;
      }
      if (id == Id::IronOre) {
        return Id::IronIngot;
      }
      if (id == Id::RawBeef) {
        return Id::CookedBeef;
      }
      if (id == Id::RawPork) {
        return Id::CookedPork;
      }
      return Id::Air;
    };
    const Id want = cook_out(f.input.id);
    if (want == Id::Air || f.input.empty()) {
      f.cook = 0.f;
      continue;
    }
    if (f.fuel_left <= 0.f) {
      const float v = fuel_value(f.fuel.id);
      if (v <= 0.f) {
        f.cook = 0.f;
        continue;
      }
      f.fuel_left = v;
      f.fuel.count--;
      if (f.fuel.count == 0) {
        f.fuel.id = Id::Air;
      }
    }
    f.fuel_left -= dt;
    f.cook += dt;
    if (f.cook >= 8.f) {
      f.cook = 0.f;
      f.input.count--;
      if (f.input.count == 0) {
        f.input.id = Id::Air;
      }
      if (f.output.empty() || f.output.id == want) {
        f.output.id = want;
        if (f.output.count < 64) {
          f.output.count++;
        }
      }
    }
  }
}

}  // namespace mc
