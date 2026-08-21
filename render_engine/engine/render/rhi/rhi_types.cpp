#include "engine/rhi/i_device.h"

#include <vector>

namespace engine::rhi {

Status IDevice::DrawTexturedQuads(std::span<const TexturedQuad> quads) {
  if (quads.empty()) {
    return Status::Ok();
  }
  std::vector<UiVertex> verts;
  std::vector<std::uint16_t> indices;
  verts.reserve(quads.size() * 4);
  indices.reserve(quads.size() * 6);
  for (const auto& q : quads) {
    const auto base = static_cast<std::uint16_t>(verts.size());
    auto push = [&](float x, float y, float u, float v) {
      UiVertex vt;
      vt.x = x;
      vt.y = y;
      vt.u = u;
      vt.v = v;
      vt.r = q.color.r;
      vt.g = q.color.g;
      vt.b = q.color.b;
      vt.a = q.color.a;
      verts.push_back(vt);
    };
    push(q.x0, q.y0, q.u0, q.v0);
    push(q.x1, q.y0, q.u1, q.v0);
    push(q.x1, q.y1, q.u1, q.v1);
    push(q.x0, q.y1, q.u0, q.v1);
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
  }
  UiDrawCmd cmd;
  cmd.index_offset = 0;
  cmd.index_count = static_cast<std::uint32_t>(indices.size());
  cmd.clip_x0 = 0;
  cmd.clip_y0 = 0;
  cmd.clip_x1 = static_cast<float>(width());
  cmd.clip_y1 = static_cast<float>(height());
  return DrawUiMesh(verts, indices, std::span<const UiDrawCmd>(&cmd, 1));
}

}  // namespace engine::rhi
