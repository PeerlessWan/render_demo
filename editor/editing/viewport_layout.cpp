#include "editing/viewport_layout.h"

namespace editor {

void LayoutViewports(int mode, float width, float height, ViewportPane out[4], int* count) {
  if (!out || !count) {
    return;
  }
  if (mode != 1) {
    out[0] = {0.f, 0.f, width, height, 0};
    *count = 1;
    return;
  }
  const float hx = width * 0.5f;
  const float hy = height * 0.5f;
  out[0] = {0.f, 0.f, hx, hy, 0};
  out[1] = {hx, 0.f, width, hy, 1};
  out[2] = {0.f, hy, hx, height, 2};
  out[3] = {hx, hy, width, height, 3};
  *count = 4;
}

int PaneAt(const ViewportPane* panes, int count, float mx, float my) {
  if (!panes) {
    return 0;
  }
  for (int i = 0; i < count; ++i) {
    if (mx >= panes[i].x0 && mx < panes[i].x1 && my >= panes[i].y0 && my < panes[i].y1) {
      return i;
    }
  }
  return 0;
}

void ApplyPaneCamera(int pane, engine::render::Camera* cam, const engine::render::Camera& persp) {
  if (!cam) {
    return;
  }
  if (pane <= 0) {
    *cam = persp;
    return;
  }
  if (pane == 1) {
    cam->position = {0.f, 18.f, 0.01f};
    cam->yaw = 0.f;
    cam->pitch = -1.55f;
    return;
  }
  if (pane == 2) {
    cam->position = {0.f, 2.f, 16.f};
    cam->yaw = 0.f;
    cam->pitch = 0.f;
    return;
  }
  cam->position = {16.f, 2.f, 0.f};
  cam->yaw = -1.57f;
  cam->pitch = 0.f;
}

float PaneAspect(const ViewportPane& pane) {
  const float w = pane.x1 - pane.x0;
  const float h = pane.y1 - pane.y0;
  return (h > 1.f) ? (w / h) : 1.f;
}

}  // namespace editor
