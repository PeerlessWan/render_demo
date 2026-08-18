#include "editing/snap.h"

#include <cmath>

namespace editor {

float SnapScalar(float v, float grid) {
  if (!(grid > 1e-4f)) {
    return v;
  }
  return std::round(v / grid) * grid;
}

engine::Vec3 SnapVec3(const engine::Vec3& v, float grid) {
  return {SnapScalar(v.x, grid), SnapScalar(v.y, grid), SnapScalar(v.z, grid)};
}

void SnapTransform(engine::scene::Transform* t, float grid) {
  if (!t) {
    return;
  }
  t->position = SnapVec3(t->position, grid);
}

}  // namespace editor
