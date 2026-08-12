#include "engine/render/shadow_atlas.h"

namespace engine::render {

bool ShadowAtlas::Allocate(int light_id, int extent, ShadowAtlasSlot& out) {
  if (extent <= 0 || extent > size_) {
    return false;
  }
  if (cursor_x_ + extent > size_) {
    cursor_x_ = 0;
    cursor_y_ += row_h_;
    row_h_ = 0;
  }
  if (cursor_y_ + extent > size_) {
    return false;
  }
  out.light_id = light_id;
  out.x = cursor_x_;
  out.y = cursor_y_;
  out.w = extent;
  out.h = extent;
  slots_.push_back(out);
  cursor_x_ += extent;
  row_h_ = row_h_ > extent ? row_h_ : extent;
  return true;
}

void ShadowAtlas::Reset() {
  slots_.clear();
  cursor_x_ = cursor_y_ = row_h_ = 0;
}

}  // namespace engine::render
