#include "engine/clothing/garment_cloth.h"
#include "engine/core/feature.h"
#include "engine/core/log.h"
#include "engine/gameplay/possess_controller.h"
#include "engine/terrain/chunk_stream.h"
#include "engine/terrain/heightmap.h"

#include <cstdlib>
#include <filesystem>
#include <string>

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

  engine::LogInfo("Learn 39 — Mega-W10 deepen smoke (ADR 0037)");

  // Large terrain path
  const auto hm_path = std::filesystem::path(ENGINE_CONTENT_DIR_A) / "scenes" / "large_terrain" /
                       "heightmap_512.png";
  auto hm = engine::terrain::LoadHeightmapPng(hm_path, 2.f, 40.f);
  if (!hm) {
    engine::LogInfo("LoadHeightmapPng SKIP: " + hm.status().message());
  } else {
    engine::LogInfo("Heightmap Ok " + std::to_string(hm->width) + "x" +
                    std::to_string(hm->height));
    engine::terrain::TerrainChunkStreamer streamer;
    streamer.ConfigureForHeightmap(hm.value(), 8, 1,
                                   engine::terrain::EstimateHeightChunkBytes(32));
    engine::assets::StreamingBudget budget(1u << 20);
    streamer.Update({100.f, 20.f, 100.f}, budget);
    engine::LogInfo("ChunkStream resident=" + std::to_string(streamer.resident_count()));
  }

  // Clothing
  engine::clothing::GarmentCloth cape;
  engine::clothing::GarmentMeshDesc desc;
  desc.kind = engine::clothing::GarmentKind::Cape;
  cape.Generate(desc, {0.f, 1.5f, 0.f});
  cape.Step(1.f / 60.f, nullptr);
  engine::LogInfo(std::string("Garment AllFinite=") + (cape.AllFinite() ? "true" : "false"));

  // Possess toggle
  engine::gameplay::PossessController possess;
  possess.possess_character = false;
  engine::gameplay::PossessInput input;
  input.move_z = 1.f;
  possess.Step(1.f / 60.f, input);
  engine::LogInfo("possess_character=false (free cam) pos.z=" + std::to_string(possess.position.z));
  possess.possess_character = true;
  if (hm) {
    possess.SetSampleHeight(
        [&](float x, float z) { return engine::terrain::SampleHeight(hm.value(), x, z); });
  } else {
    possess.SetSampleHeight([](float, float) { return 0.f; });
  }
  possess.position = {50.f, 5.f, 50.f};
  for (int i = 0; i < 10; ++i) {
    possess.Step(1.f / 60.f, input);
  }
  engine::LogInfo("possess_character=true after walk y=" + std::to_string(possess.position.y));
  const auto cam = possess.ThirdPersonCameraPosition(0.f);
  engine::LogInfo("ThirdPersonCamera y=" + std::to_string(cam.y));

  const auto features = engine::QueryFeatures();
  engine::LogInfo(std::string("Features virtual_texture=") +
                  (features.virtual_texture ? "true" : "false") +
                  " bindless=" + (features.bindless ? "true" : "false"));

  (void)headless_frames;
  return 0;
}
