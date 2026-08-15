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

  // Obstacle for character collision.
  engine::physics::RigidBodyDesc wall;
  wall.position = {2.f, 1.f, 0.f};
  wall.half_extents = {0.5f, 1.f, 2.f};
  wall.mass = 0.f;
  (void)world->CreateBox(wall);

  engine::physics::RigidBodyDesc box;
  box.position = {0.f, 3.f, 0.f};
  box.half_extents = {0.5f, 0.5f, 0.5f};
  const int body = world->CreateBox(box);

  engine::physics::CapsuleDesc character;
  character.position = {0.f, 1.f, 0.f};
  character.radius = 0.35f;
  character.half_height = 0.5f;
  character.mass = 0.f;  // kinematic
  const int char_id = world->CreateCapsule(character);
  if (char_id < 0) {
    engine::LogError("CreateCapsule failed");
    return 1;
  }

  for (int i = 0; i < 4; ++i) {
    world->Step(1.f / 60.f);
  }
  const auto pos = world->body_position(body);
  engine::LogInfo("Body y after sim=" + std::to_string(pos.y));

  const auto hit = world->Raycast({0.f, 5.f, 0.f}, {0.f, -1.f, 0.f}, 20.f);
  engine::LogInfo(std::string("Raycast hit=") + (hit.hit ? "true" : "false"));

  // Simulated WASD walk (headless): W forward (-Z), A left, S back, D right.
  constexpr float kStep = 0.25f;
  const engine::Vec3 wasd[] = {
      {0.f, 0.f, -kStep},  // W
      {-kStep, 0.f, 0.f},  // A
      {0.f, 0.f, kStep},   // S
      {kStep, 0.f, 0.f},   // D
      {kStep, 0.f, -kStep} // WD diagonal toward wall
  };
  for (const auto& d : wasd) {
    const auto st = world->MoveCharacter(char_id, d);
    if (!st) {
      engine::LogError(std::string("MoveCharacter failed: ") + st.message());
      return 1;
    }
    world->Step(1.f / 60.f);
  }
  const auto cpos = world->body_position(char_id);
  engine::LogInfo("Character after WASD x=" + std::to_string(cpos.x) +
                  " y=" + std::to_string(cpos.y) + " z=" + std::to_string(cpos.z));

  (void)headless_frames;
  return 0;
}
