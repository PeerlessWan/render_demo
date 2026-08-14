#include "engine/core/log.h"
#include "engine/physics/i_physics_world.h"

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

  auto world = engine::physics::CreateDefaultPhysicsWorld();
  if (!world) {
    engine::LogError("CreateDefaultPhysicsWorld failed");
    return 1;
  }
  engine::LogInfo(std::string("Physics backend: ") + world->backend_name());

  engine::physics::RigidBodyDesc floor;
  floor.position = {0.f, -0.5f, 0.f};
  floor.half_extents = {10.f, 0.5f, 10.f};
  floor.mass = 0.f;
  (void)world->CreateBox(floor);

  engine::physics::RigidBodyDesc box;
  box.position = {0.f, 3.f, 0.f};
  box.half_extents = {0.5f, 0.5f, 0.5f};
  const int body = world->CreateBox(box);

  for (int i = 0; i < 4; ++i) {
    world->Step(1.f / 60.f);
  }
  const auto pos = world->body_position(body);
  engine::LogInfo("Body y after sim=" + std::to_string(pos.y));

  const auto hit = world->Raycast({0.f, 5.f, 0.f}, {0.f, -1.f, 0.f}, 20.f);
  engine::LogInfo(std::string("Raycast hit=") + (hit.hit ? "true" : "false"));

  (void)headless_frames;
  return 0;
}
