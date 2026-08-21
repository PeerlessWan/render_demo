#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::render2d {

// Godot CanvasItem / Node2D lite (ADR 0049).
struct CanvasItem {
  int id = -1;
  int parent = -1;  // -1 = root
  std::string name;
  Vec2 position{};
  float rotation = 0.f;  // radians
  Vec2 scale{1.f, 1.f};
  bool visible = true;
  int z_index = 0;
  bool y_sort = false;
  ColorRgba modulate{1.f, 1.f, 1.f, 1.f};
  std::uint32_t layer = 1u;
  // Optional sprite payload (empty atlas → solid rect).
  std::string atlas_id;
  int frame = 0;
  Vec2 size{16.f, 16.f};
  ColorRgba color{1.f, 1.f, 1.f, 1.f};
};

struct CanvasWorldTransform {
  Vec2 position{};
  float rotation = 0.f;
  Vec2 scale{1.f, 1.f};
  ColorRgba modulate{1.f, 1.f, 1.f, 1.f};
  bool visible = true;
  int z_index = 0;
  float sort_y = 0.f;
};

class CanvasItemTree {
 public:
  int Create(std::string name = {}, int parent = -1);
  [[nodiscard]] bool valid(int id) const;
  CanvasItem* get(int id);
  [[nodiscard]] const CanvasItem* get(int id) const;
  void set_parent(int id, int parent);
  void Clear();

  // Fills world transforms for all items (skips invisible subtrees).
  void ComputeWorld(std::vector<CanvasWorldTransform>* out) const;

  // Flatten visible sprites for batching (sort by z then y when y_sort).
  void FlattenSprites(std::vector<struct Sprite>* out) const;

  [[nodiscard]] const std::vector<CanvasItem>& items() const { return items_; }

 private:
  std::vector<CanvasItem> items_;
  int next_id_ = 1;
};

}  // namespace engine::render2d
