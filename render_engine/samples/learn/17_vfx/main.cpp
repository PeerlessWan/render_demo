#include "engine/core/log.h"
#include "engine/debug/debug_draw.h"
#include "engine/vfx/particles.h"
#include "engine/vfx/trail_ribbon.h"

#include <cstdlib>
#include <string>

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

  engine::vfx::ParticleEmitter emitter;
  emitter.Configure({0.f, 1.f, 0.f}, 40.f, 1.0f);
  emitter.EmitBurst(16);
  for (int i = 0; i < 8; ++i) {
    emitter.Step(1.f / 60.f);
  }
  engine::LogInfo("Particles alive=" + std::to_string(emitter.particles().size()));

  engine::vfx::TrailRibbon trail;
  trail.Configure(0.5f, 0.08f, 24);
  for (int i = 0; i < 10; ++i) {
    trail.Push({static_cast<float>(i) * 0.1f, 1.f, 0.f});
    trail.Step(1.f / 60.f);
  }
  const auto segs = trail.BuildSegments();
  engine::LogInfo("Trail points=" + std::to_string(trail.points().size()) +
                  " segments=" + std::to_string(segs.size()));

  engine::debug::DebugDraw draw;
  trail.AppendDebugLines(draw);
  engine::LogInfo("DebugDraw lines appended (trail proxy; no Decal GPU path in this sample)");

  (void)headless_frames;
  return 0;
}
