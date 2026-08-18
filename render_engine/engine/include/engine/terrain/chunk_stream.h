#pragma once

#include "engine/assets/streaming_budget.h"
#include "engine/core/math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::terrain {

// Mega-W9: terrain/vegetation chunk streaming keyed by XZ grid + StreamingBudget.

struct ChunkKey {
  int x = 0;
  int z = 0;

  bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey& k) const {
    const auto ux = static_cast<std::uint32_t>(k.x);
    const auto uz = static_cast<std::uint32_t>(k.z);
    return (static_cast<std::size_t>(ux) * 73856093u) ^
           (static_cast<std::size_t>(uz) * 19349663u);
  }
};

[[nodiscard]] inline ChunkKey WorldToChunk(float world_x, float world_z, float chunk_size) {
  const float cs = chunk_size > 1e-4f ? chunk_size : 1.f;
  return ChunkKey{static_cast<int>(std::floor(world_x / cs)),
                  static_cast<int>(std::floor(world_z / cs))};
}

[[nodiscard]] inline std::string ChunkAssetId(const ChunkKey& key) {
  return "terrain/chunk_" + std::to_string(key.x) + "_" + std::to_string(key.z);
}

class TerrainChunkStreamer {
 public:
  void Configure(float chunk_world_size, int load_radius_chunks, std::size_t bytes_per_chunk);

  // Load/unload around camera chunk; integrates with StreamingBudget residency.
  void Update(const Vec3& camera_world, assets::StreamingBudget& budget);

  [[nodiscard]] const std::unordered_set<ChunkKey, ChunkKeyHash>& resident() const {
    return resident_;
  }
  [[nodiscard]] std::uint32_t load_count() const { return load_count_; }
  [[nodiscard]] std::uint32_t unload_count() const { return unload_count_; }
  [[nodiscard]] std::uint32_t resident_count() const {
    return static_cast<std::uint32_t>(resident_.size());
  }
  [[nodiscard]] ChunkKey last_camera_chunk() const { return last_cam_chunk_; }

  void ResetCounters() {
    load_count_ = 0;
    unload_count_ = 0;
  }

 private:
  float chunk_size_ = 64.f;
  int load_radius_ = 1;
  std::size_t bytes_per_chunk_ = 1024;
  std::unordered_set<ChunkKey, ChunkKeyHash> resident_;
  ChunkKey last_cam_chunk_{};
  std::uint32_t load_count_ = 0;
  std::uint32_t unload_count_ = 0;
};

}  // namespace engine::terrain
