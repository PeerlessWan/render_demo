#include "engine/assets/streaming_budget.h"
#include "engine/core/log.h"
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

  engine::LogInfo("Learn 38 — large terrain heightmap + ChunkStream (ADR 0037)");

  const auto path = std::filesystem::path(ENGINE_CONTENT_DIR_A) / "scenes" / "large_terrain" /
                    "heightmap_512.png";
  auto loaded = engine::terrain::LoadHeightmapPng(path, /*cell=*/2.f, /*height_scale=*/40.f);
  if (!loaded) {
    engine::LogError("LoadHeightmapPng failed: " + loaded.status().message());
    return 1;
  }
  const engine::terrain::Heightmap& map = loaded.value();
  engine::LogInfo("Heightmap " + std::to_string(map.width) + "x" + std::to_string(map.height) +
                  " cell=" + std::to_string(map.cell) +
                  " worldXZ=" + std::to_string(engine::terrain::HeightmapWorldSizeX(map)) + "x" +
                  std::to_string(engine::terrain::HeightmapWorldSizeZ(map)));

  const float mid_x = 0.5f * engine::terrain::HeightmapWorldSizeX(map);
  const float mid_z = 0.5f * engine::terrain::HeightmapWorldSizeZ(map);
  const float h0 = engine::terrain::SampleHeight(map, mid_x, mid_z);
  engine::LogInfo("SampleHeight center=" + std::to_string(h0));

  engine::terrain::TerrainChunkStreamer streamer;
  const std::size_t bytes =
      engine::terrain::EstimateHeightChunkBytes(64);
  streamer.ConfigureForHeightmap(map, /*chunks_along_short_axis=*/8, /*load_radius=*/1, bytes);
  engine::assets::StreamingBudget budget(bytes * 32);

  streamer.Update({mid_x, h0 + 5.f, mid_z}, budget);
  engine::LogInfo("ChunkStream resident=" + std::to_string(streamer.resident_count()) +
                  " loads=" + std::to_string(streamer.load_count()) +
                  " budget_used=" + std::to_string(budget.used()));

  streamer.ResetCounters();
  streamer.Update({mid_x + 200.f, h0 + 5.f, mid_z}, budget);
  engine::LogInfo("After move loads=" + std::to_string(streamer.load_count()) +
                  " unloads=" + std::to_string(streamer.unload_count()) +
                  " resident=" + std::to_string(streamer.resident_count()));

  (void)headless_frames;
  return 0;
}
