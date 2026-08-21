#include "engine/render2d/canvas_item.h"

#include "engine/render2d/sprite.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace engine::render2d {
namespace {

CanvasWorldTransform Mul(const CanvasWorldTransform& a, const CanvasItem& b) {
  CanvasWorldTransform o;
  const float c = std::cos(a.rotation);
  const float s = std::sin(a.rotation);
  const float lx = b.position.x * a.scale.x;
  const float ly = b.position.y * a.scale.y;
  o.position.x = a.position.x + c * lx - s * ly;
  o.position.y = a.position.y + s * lx + c * ly;
  o.rotation = a.rotation + b.rotation;
  o.scale.x = a.scale.x * b.scale.x;
  o.scale.y = a.scale.y * b.scale.y;
  o.modulate.r = a.modulate.r * b.modulate.r;
  o.modulate.g = a.modulate.g * b.modulate.g;
  o.modulate.b = a.modulate.b * b.modulate.b;
  o.modulate.a = a.modulate.a * b.modulate.a;
  o.visible = a.visible && b.visible;
  o.z_index = a.z_index + b.z_index;
  o.sort_y = o.position.y;
  return o;
}

}  // namespace

int CanvasItemTree::Create(std::string name, int parent) {
  CanvasItem it;
  it.id = next_id_++;
  it.parent = parent;
  it.name = std::move(name);
  items_.push_back(std::move(it));
  return items_.back().id;
}

bool CanvasItemTree::valid(int id) const {
  for (const auto& it : items_) {
    if (it.id == id) {
      return true;
    }
  }
  return false;
}

CanvasItem* CanvasItemTree::get(int id) {
  for (auto& it : items_) {
    if (it.id == id) {
      return &it;
    }
  }
  return nullptr;
}

const CanvasItem* CanvasItemTree::get(int id) const {
  for (const auto& it : items_) {
    if (it.id == id) {
      return &it;
    }
  }
  return nullptr;
}

void CanvasItemTree::set_parent(int id, int parent) {
  if (auto* it = get(id)) {
    it->parent = parent;
  }
}

void CanvasItemTree::Clear() {
  items_.clear();
  next_id_ = 1;
}

void CanvasItemTree::ComputeWorld(std::vector<CanvasWorldTransform>* out) const {
  if (!out) {
    return;
  }
  out->assign(items_.size(), CanvasWorldTransform{});
  std::unordered_map<int, std::size_t> idx;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    idx[items_[i].id] = i;
  }
  std::vector<char> done(items_.size(), 0);
  auto resolve = [&](auto&& self, std::size_t i) -> CanvasWorldTransform {
    if (done[i]) {
      return (*out)[i];
    }
    const auto& it = items_[i];
    CanvasWorldTransform parent_w;
    parent_w.visible = true;
    parent_w.modulate = {1, 1, 1, 1};
    parent_w.scale = {1, 1};
    if (it.parent >= 0) {
      auto pit = idx.find(it.parent);
      if (pit != idx.end()) {
        parent_w = self(self, pit->second);
      }
    }
    (*out)[i] = Mul(parent_w, it);
    done[i] = 1;
    return (*out)[i];
  };
  for (std::size_t i = 0; i < items_.size(); ++i) {
    resolve(resolve, i);
  }
}

void CanvasItemTree::FlattenSprites(std::vector<Sprite>* out) const {
  if (!out) {
    return;
  }
  out->clear();
  std::vector<CanvasWorldTransform> world;
  ComputeWorld(&world);
  struct Row {
    Sprite spr;
    int z = 0;
    float y = 0;
    bool y_sort = false;
  };
  std::vector<Row> rows;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const auto& it = items_[i];
    const auto& w = world[i];
    if (!w.visible) {
      continue;
    }
    Sprite spr;
    spr.atlas_id = it.atlas_id;
    spr.frame = it.frame;
    spr.position = w.position;
    spr.size = {it.size.x * w.scale.x, it.size.y * w.scale.y};
    spr.sort_layer = w.z_index;
    spr.sort_y = w.sort_y;
    spr.color = {it.color.r * w.modulate.r, it.color.g * w.modulate.g, it.color.b * w.modulate.b,
                 it.color.a * w.modulate.a};
    spr.modulate = w.modulate;
    spr.layer_mask = it.layer;
    rows.push_back(Row{std::move(spr), w.z_index, w.sort_y, it.y_sort});
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.z != b.z) {
      return a.z < b.z;
    }
    if (a.y_sort || b.y_sort) {
      return a.y < b.y;
    }
    return false;
  });
  out->reserve(rows.size());
  for (auto& r : rows) {
    out->push_back(std::move(r.spr));
  }
}

}  // namespace engine::render2d
