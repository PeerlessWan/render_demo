#pragma once

#include "engine/scene/world.h"

namespace editor {

[[nodiscard]] float SnapScalar(float v, float grid);
[[nodiscard]] engine::Vec3 SnapVec3(const engine::Vec3& v, float grid);
void SnapTransform(engine::scene::Transform* t, float grid);

}  // namespace editor
