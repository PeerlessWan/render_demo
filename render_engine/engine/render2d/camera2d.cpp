#include "engine/render2d/camera2d.h"

#include <algorithm>
#include <cmath>

namespace engine::render2d {

void Camera2DFollow(Camera2D* cam, const Vec2& target, float dt) {
  if (!cam) {
    return;
  }
  if (cam->follow_smoothing <= 1e-5f || dt <= 0.f) {
    cam->position = target;
  } else {
    const float t = 1.f - std::exp(-cam->follow_smoothing * dt);
    cam->position.x += (target.x - cam->position.x) * t;
    cam->position.y += (target.y - cam->position.y) * t;
  }
  Camera2DClampLimits(cam);
}

void Camera2DClampLimits(Camera2D* cam) {
  if (!cam) {
    return;
  }
  cam->position.x = std::clamp(cam->position.x, cam->limit_min.x, cam->limit_max.x);
  cam->position.y = std::clamp(cam->position.y, cam->limit_min.y, cam->limit_max.y);
}

float Camera2DPixelsPerUnit(const Camera2D& cam, float view_w, float view_h) {
  const float zx = view_w / std::max(1.f, cam.design_size.x);
  const float zy = view_h / std::max(1.f, cam.design_size.y);
  float s = std::min(zx, zy) * std::max(0.01f, cam.zoom);
  if (cam.integer_scale) {
    s = std::max(1.f, std::floor(s));
  }
  return s;
}

Vec2 Camera2DWorldToScreen(const Camera2D& cam, Vec2 world, float view_w, float view_h) {
  const float ppu = Camera2DPixelsPerUnit(cam, view_w, view_h);
  const Vec2 shake = cam.shake.Offset();
  const float cx = cam.position.x + cam.offset.x + shake.x;
  const float cy = cam.position.y + cam.offset.y + shake.y;
  float dx = world.x - cx;
  float dy = world.y - cy;
  if (std::fabs(cam.rotation) > 1e-6f) {
    const float c = std::cos(-cam.rotation);
    const float s = std::sin(-cam.rotation);
    const float rx = c * dx - s * dy;
    const float ry = s * dx + c * dy;
    dx = rx;
    dy = ry;
  }
  return {view_w * 0.5f + dx * ppu, view_h * 0.5f + dy * ppu};
}

Vec2 Camera2DScreenToWorld(const Camera2D& cam, Vec2 screen, float view_w, float view_h) {
  const float ppu = Camera2DPixelsPerUnit(cam, view_w, view_h);
  const Vec2 shake = cam.shake.Offset();
  float dx = (screen.x - view_w * 0.5f) / ppu;
  float dy = (screen.y - view_h * 0.5f) / ppu;
  if (std::fabs(cam.rotation) > 1e-6f) {
    const float c = std::cos(cam.rotation);
    const float s = std::sin(cam.rotation);
    const float rx = c * dx - s * dy;
    const float ry = s * dx + c * dy;
    dx = rx;
    dy = ry;
  }
  return {cam.position.x + cam.offset.x + shake.x + dx,
          cam.position.y + cam.offset.y + shake.y + dy};
}

}  // namespace engine::render2d
