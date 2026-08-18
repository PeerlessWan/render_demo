#pragma once

#include "engine/render/camera.h"

namespace editor {

struct ViewportPane {
  float x0 = 0.f;
  float y0 = 0.f;
  float x1 = 0.f;
  float y1 = 0.f;
  int index = 0;
};

// 0 single, 1 quad 2x2 (Persp/Top/Front/Side).
void LayoutViewports(int mode, float width, float height, ViewportPane out[4], int* count);

[[nodiscard]] int PaneAt(const ViewportPane* panes, int count, float mx, float my);

void ApplyPaneCamera(int pane, engine::render::Camera* cam, const engine::render::Camera& persp);

[[nodiscard]] float PaneAspect(const ViewportPane& pane);

}  // namespace editor
