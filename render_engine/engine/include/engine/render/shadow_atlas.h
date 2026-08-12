#pragma once

#include "engine/core/math.h"

#include <vector>

namespace engine::render {

struct ShadowAtlasSlot {
  int light_id = -1;
  int x = 0, y = 0, w = 0, h = 0;
};

// M11: pack point/spot shadow maps into an atlas (CPU packing only for skeleton).
class ShadowAtlas {
 public:
  explicit ShadowAtlas(int size = 2048) : size_(size) {}
  bool Allocate(int light_id, int extent, ShadowAtlasSlot& out);
  void Reset();
  [[nodiscard]] int size() const { return size_; }
  [[nodiscard]] const std::vector<ShadowAtlasSlot>& slots() const { return slots_; }

 private:
  int size_ = 2048;
  int cursor_x_ = 0;
  int cursor_y_ = 0;
  int row_h_ = 0;
  std::vector<ShadowAtlasSlot> slots_;
};

}  // namespace engine::render
