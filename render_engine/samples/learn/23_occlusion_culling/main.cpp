#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/render/occlusion.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace {

void ParseHeadless(int argc, char** argv, int& headless_frames) {
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      headless = true;
      if (headless_frames <= 0) {
        headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      headless_frames = std::atoi(argv[++i]);
    }
  }
  if (headless && headless_frames <= 0) {
    headless_frames = 2;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int headless_frames = 0;
  ParseHeadless(argc, argv, headless_frames);

  engine::render::OcclusionBuffer occ;
  constexpr int kW = 32;
  constexpr int kH = 32;
  occ.Configure(kW, kH);

  std::vector<float> depth(static_cast<std::size_t>(kW * kH), 1.f);
  // Near wall in the center: smaller depth = closer.
  for (int y = 8; y < 24; ++y) {
    for (int x = 8; x < 24; ++x) {
      depth[static_cast<std::size_t>(y * kW + x)] = 0.2f;
    }
  }
  occ.UploadDepthFinest(depth);
  engine::LogInfo("OcclusionBuffer " + std::to_string(occ.width()) + "x" +
                  std::to_string(occ.height()) + " levels=" + std::to_string(occ.pyramid_levels()) +
                  " has_hiz=" + (occ.has_hiz() ? "true" : "false"));

  // Identity-ish view_proj for teaching: treat AABB in NDC-ish space.
  const engine::Mat4 view_proj = engine::Mat4::Identity();
  engine::Aabb front;
  front.min = {-0.1f, -0.1f, 0.05f};
  front.max = {0.1f, 0.1f, 0.15f};
  engine::Aabb behind;
  behind.min = {-0.05f, -0.05f, 0.5f};
  behind.max = {0.05f, 0.05f, 0.6f};

  const bool v_front = occ.IsVisible(front, view_proj);
  const bool v_behind = occ.IsVisible(behind, view_proj);
  engine::LogInfo(std::string("IsVisible front=") + (v_front ? "true" : "false") +
                  " behind_center=" + (v_behind ? "true" : "false"));

  occ.ClearHiZ();
  engine::LogInfo(std::string("After ClearHiZ has_hiz=") + (occ.has_hiz() ? "true" : "false") +
                  " (frustum-only fallback)");

  (void)headless_frames;
  return 0;
}
