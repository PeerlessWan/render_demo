#include "engine/assets/character_asset.h"
#include "engine/assets/image_loader.h"
#include "engine/clothing/garment_cloth.h"
#include "engine/core/log.h"
#include "engine/physics/i_physics_world.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef ENGINE_CONTENT_DIR_A
#error "ENGINE_CONTENT_DIR_A must be set by CMake"
#endif

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

  engine::LogInfo("Learn 37 — clothing / demo garment SoftBody (ADR 0037 / W11 character)");

  auto images = engine::assets::CreateDefaultImageLoader();
  const auto characters_dir =
      std::filesystem::path(ENGINE_CONTENT_DIR_A) / "characters";
  const auto character = engine::assets::CharacterAsset::TryLoadFromCharactersDirOrCapsule(
      characters_dir, *images);
  engine::LogInfo(std::string("CharacterAsset: ") + character.note +
                  " verts=" + std::to_string(character.mesh.vertices.size()) +
                  " fallback=" + (character.used_capsule_fallback ? "true" : "false") +
                  " skin=" + (character.has_skin ? "true" : "false"));

  engine::clothing::GarmentMeshDesc cape_desc;
  cape_desc.kind = engine::clothing::GarmentKind::Cape;
  cape_desc.rows = 6;
  cape_desc.cols = 5;

  engine::clothing::GarmentCloth cape;
  cape.Generate(cape_desc, {0.f, 1.6f, 0.f});
  engine::LogInfo("Cape verts=" + std::to_string(cape.positions.size()) +
                  " tris~=" + std::to_string(cape.indices.size() / 3));

  std::vector<engine::Vec3> attach;
  for (int c = 0; c < cape_desc.cols; ++c) {
    const float u = static_cast<float>(c) / static_cast<float>(cape_desc.cols - 1);
    attach.push_back({(u - 0.5f) * cape_desc.width, 1.6f, 0.f});
  }
  cape.SetAttachPoints(attach);

  engine::clothing::CapsuleCollider body;
  body.center = {0.f, 0.9f, 0.f};
  body.radius = 0.35f;
  body.half_height = 0.55f;

  auto world = engine::physics::CreateDefaultPhysicsWorld();
  const char* backend = world ? world->backend_name() : "none";
  engine::LogInfo(std::string("Physics backend: ") + backend);

  bool wired = false;
  if (world) {
    wired = cape.TryWirePhysicsSoftBody(*world, {0.f, 1.4f, -0.2f});
  }
  engine::LogInfo(std::string("TryWirePhysicsSoftBody=") + (wired ? "Ok" : "SKIP (Verlet only)"));

  for (int i = 0; i < 30; ++i) {
    if (world) {
      world->Step(1.f / 60.f);
    }
    cape.Step(1.f / 60.f, &body);
    if (wired && world) {
      (void)cape.SyncFromPhysics(*world);
    }
  }

  engine::LogInfo(std::string("Garment AllFinite=") + (cape.AllFinite() ? "true" : "false"));
  if (!cape.positions.empty()) {
    const auto& tip = cape.positions.back();
    engine::LogInfo("Cape tip y=" + std::to_string(tip.y));
  }

  engine::clothing::GarmentMeshDesc skirt_desc;
  skirt_desc.kind = engine::clothing::GarmentKind::Skirt;
  skirt_desc.rows = 5;
  skirt_desc.cols = 8;
  engine::clothing::GarmentCloth skirt;
  skirt.Generate(skirt_desc, {0.f, 1.0f, 0.f});
  skirt.Step(1.f / 60.f, &body);
  engine::LogInfo("Skirt verts=" + std::to_string(skirt.positions.size()) +
                  " AllFinite=" + (skirt.AllFinite() ? "true" : "false"));

  (void)headless_frames;
  return 0;
}
