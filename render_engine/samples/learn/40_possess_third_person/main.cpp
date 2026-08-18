#include "engine/core/log.h"
#include "engine/gameplay/possess_controller.h"
#include "engine/terrain/heightmap.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace {

void ParseHeadless(int argc, char** argv, int& headless_frames, bool& force_possess) {
  bool headless = false;
  force_possess = true;
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
    } else if (arg == "--free-camera") {
      force_possess = false;
    } else if (arg == "--possess") {
      force_possess = true;
    }
  }
  if (headless && headless_frames <= 0) {
    headless_frames = 2;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int headless_frames = 0;
  bool force_possess = true;
  ParseHeadless(argc, argv, headless_frames, force_possess);

  engine::LogInfo("Learn 40 — possess third-person (ADR 0037)");

  // Procedural gentle hills (no content dependency).
  engine::terrain::Heightmap map;
  map.width = 65;
  map.height = 65;
  map.cell = 1.f;
  map.samples.resize(static_cast<std::size_t>(65 * 65));
  for (int z = 0; z < 65; ++z) {
    for (int x = 0; x < 65; ++x) {
      const float nx = static_cast<float>(x) / 64.f;
      const float nz = static_cast<float>(z) / 64.f;
      map.samples[static_cast<std::size_t>(z * 65 + x)] =
          0.5f + 1.5f * std::sin(nx * 6.f) * std::cos(nz * 5.f);
    }
  }

  const auto mesh = engine::gameplay::BuildCapsuleCharacterMesh(0.35f, 1.8f, 6, 10);
  engine::LogInfo("Capsule mesh verts=" + std::to_string(mesh.positions.size()) +
                  " indices=" + std::to_string(mesh.indices.size()));

  engine::gameplay::PossessController ctrl;
  ctrl.possess_character = force_possess;
  ctrl.SetSampleHeight(
      [&](float x, float z) { return engine::terrain::SampleHeight(map, x, z); });
  ctrl.AddObstacle({{20.f, 0.f, 20.f}, {22.f, 3.f, 28.f}});
  ctrl.position = {32.f, 5.f, 32.f};

  engine::LogInfo(std::string("possess_character=") +
                  (ctrl.possess_character ? "true" : "false (free camera)"));

  engine::gameplay::PossessInput input;
  input.yaw = 0.4f;
  input.move_z = 1.f;
  for (int i = 0; i < 45; ++i) {
    if (i == 20) {
      input.jump = true;
    } else {
      input.jump = false;
    }
    ctrl.Step(1.f / 60.f, input);
  }

  engine::LogInfo("After walk+jump pos=(" + std::to_string(ctrl.position.x) + "," +
                  std::to_string(ctrl.position.y) + "," + std::to_string(ctrl.position.z) +
                  ") on_ground=" + (ctrl.on_ground ? "true" : "false"));

  const auto eye = ctrl.ThirdPersonCameraPosition(input.yaw);
  const auto look = ctrl.ThirdPersonLookAt();
  engine::LogInfo("ThirdPerson eye=(" + std::to_string(eye.x) + "," + std::to_string(eye.y) +
                  "," + std::to_string(eye.z) + ")");
  engine::LogInfo("ThirdPerson lookAt y=" + std::to_string(look.y));

  // Toggle free camera: Step becomes locomotion no-op.
  const auto frozen = ctrl.position;
  ctrl.possess_character = false;
  input.move_z = 1.f;
  ctrl.Step(1.f / 60.f, input);
  engine::LogInfo(std::string("Free-cam Step froze pos=") +
                  (std::fabs(ctrl.position.x - frozen.x) < 1e-5f &&
                           std::fabs(ctrl.position.z - frozen.z) < 1e-5f
                       ? "true"
                       : "false"));

  (void)headless_frames;
  return 0;
}
