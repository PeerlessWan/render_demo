#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/mixed/pick.h"
#include "engine/render/render_scene.h"
#include "engine/render2d/path2d.h"
#include "engine/render2d/sprite.h"

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

  std::vector<engine::render2d::Sprite> sprites(3);
  sprites[0].position = {100.f, 100.f};
  sprites[0].size = {64.f, 64.f};
  sprites[0].sort_layer = 0;
  sprites[0].sort_y = 10.f;
  sprites[1].position = {120.f, 140.f};
  sprites[1].size = {64.f, 64.f};
  sprites[1].sort_layer = 1;
  sprites[1].sort_y = 20.f;
  sprites[2].position = {80.f, 160.f};
  sprites[2].size = {64.f, 64.f};
  sprites[2].sort_layer = 1;
  sprites[2].sort_y = 5.f;
  engine::render2d::SortSprites(sprites);
  engine::LogInfo("SortSprites by layer then Y: first_layer=" +
                  std::to_string(sprites.front().sort_layer) +
                  " last_y=" + std::to_string(sprites.back().sort_y));

  const int scale = engine::mixed::IntegerScale(1920, 1080, 320, 180);
  engine::LogInfo("IntegerScale 1920x1080 vs 320x180 -> " + std::to_string(scale));

  engine::render2d::Path2D path;
  path.MoveTo({0.f, 0.f});
  path.LineTo({1.f, 0.f});
  path.LineTo({1.f, 1.f});
  path.LineTo({0.f, 0.f});  // close contour for TessellateFillFan
  const auto fill = path.TessellateFillFan();
  engine::LogInfo("Path2D fill verts=" + std::to_string(fill.vertices.size()) +
                  " indices=" + std::to_string(fill.indices.size()));

  std::vector<engine::render::RenderInstance> instances;
  engine::mixed::PickQuery q;
  q.screen_px = {130.f, 150.f};
  q.viewport_w = 1280.f;
  q.viewport_h = 720.f;
  q.inv_view_proj = engine::Mat4::Identity();
  const auto hit = engine::mixed::Pick(instances, sprites, q);
  engine::LogInfo(std::string("Pick kind=") +
                  (hit.kind == engine::mixed::PickHit::Kind::Sprite2D
                       ? "Sprite2D"
                       : (hit.kind == engine::mixed::PickHit::Kind::Scene3D ? "Scene3D" : "None")) +
                  " sprite_index=" + std::to_string(hit.sprite_index));

  (void)headless_frames;
  return 0;
}
