#include "engine/render2d/nineslice.h"

#include <algorithm>

namespace engine::render2d {

NineSliceMesh ExpandNineSlice(const NineSliceDesc& desc) {
  NineSliceMesh mesh;
  if (desc.size.x <= 0.f || desc.size.y <= 0.f || desc.source_w <= 0.f || desc.source_h <= 0.f) {
    return mesh;
  }

  const float sw = desc.source_w;
  const float sh = desc.source_h;
  float bl = std::clamp(desc.border_l, 0.f, sw);
  float br = std::clamp(desc.border_r, 0.f, sw);
  float bt = std::clamp(desc.border_t, 0.f, sh);
  float bb = std::clamp(desc.border_b, 0.f, sh);
  if (bl + br > sw) {
    const float s = sw / (bl + br);
    bl *= s;
    br *= s;
  }
  if (bt + bb > sh) {
    const float s = sh / (bt + bb);
    bt *= s;
    bb *= s;
  }

  const float du = desc.u1 - desc.u0;
  const float dv = desc.v1 - desc.v0;
  const float u_l = desc.u0 + du * (bl / sw);
  const float u_r = desc.u1 - du * (br / sw);
  const float v_t = desc.v0 + dv * (bt / sh);
  const float v_b = desc.v1 - dv * (bb / sh);

  float dl = bl;
  float dr = br;
  float dt = bt;
  float db = bb;
  if (dl + dr > desc.size.x) {
    const float s = desc.size.x / (dl + dr);
    dl *= s;
    dr *= s;
  }
  if (dt + db > desc.size.y) {
    const float s = desc.size.y / (dt + db);
    dt *= s;
    db *= s;
  }

  const float x0 = desc.position.x;
  const float x1 = x0 + dl;
  const float x2 = x0 + desc.size.x - dr;
  const float x3 = x0 + desc.size.x;
  const float y0 = desc.position.y;
  const float y1 = y0 + dt;
  const float y2 = y0 + desc.size.y - db;
  const float y3 = y0 + desc.size.y;

  const float xs[4] = {x0, x1, x2, x3};
  const float ys[4] = {y0, y1, y2, y3};
  const float us[4] = {desc.u0, u_l, u_r, desc.u1};
  const float vs[4] = {desc.v0, v_t, v_b, desc.v1};

  auto emit_quad = [&](int ix0, int iy0, int ix1, int iy1) {
    if (xs[ix1] - xs[ix0] < 1e-5f || ys[iy1] - ys[iy0] < 1e-5f) {
      return;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({{xs[ix0], ys[iy0]}, us[ix0], vs[iy0]});
    mesh.vertices.push_back({{xs[ix1], ys[iy0]}, us[ix1], vs[iy0]});
    mesh.vertices.push_back({{xs[ix1], ys[iy1]}, us[ix1], vs[iy1]});
    mesh.vertices.push_back({{xs[ix0], ys[iy1]}, us[ix0], vs[iy1]});
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
  };

  for (int iy = 0; iy < 3; ++iy) {
    for (int ix = 0; ix < 3; ++ix) {
      emit_quad(ix, iy, ix + 1, iy + 1);
    }
  }
  return mesh;
}

}  // namespace engine::render2d
