#include "engine/terrain/chunk_stream.h"

#include <memory>

namespace engine::terrain {

void TerrainChunkStreamer::Configure(float chunk_world_size, int load_radius_chunks,
                                     std::size_t bytes_per_chunk) {
  chunk_size_ = chunk_world_size > 1e-4f ? chunk_world_size : 1.f;
  load_radius_ = load_radius_chunks < 0 ? 0 : load_radius_chunks;
  bytes_per_chunk_ = bytes_per_chunk > 0 ? bytes_per_chunk : 1;
  resident_.clear();
  last_cam_chunk_ = {};
  ResetCounters();
}

void TerrainChunkStreamer::Update(const Vec3& camera_world, assets::StreamingBudget& budget) {
  last_cam_chunk_ = WorldToChunk(camera_world.x, camera_world.z, chunk_size_);

  std::unordered_set<ChunkKey, ChunkKeyHash> wanted;
  for (int dz = -load_radius_; dz <= load_radius_; ++dz) {
    for (int dx = -load_radius_; dx <= load_radius_; ++dx) {
      wanted.insert(ChunkKey{last_cam_chunk_.x + dx, last_cam_chunk_.z + dz});
    }
  }

  // Unload chunks that left the ring.
  std::vector<ChunkKey> to_unload;
  for (const auto& key : resident_) {
    if (wanted.find(key) == wanted.end()) {
      to_unload.push_back(key);
    }
  }
  for (const auto& key : to_unload) {
    budget.Release(assets::AssetId(ChunkAssetId(key)));
    resident_.erase(key);
    ++unload_count_;
  }

  // Load missing wanted chunks. Budget holds the only handle (refcount 1) so
  // EvictIfNeeded can reclaim when over budget.
  for (const auto& key : wanted) {
    if (resident_.count(key)) {
      continue;
    }
    const assets::AssetId id(ChunkAssetId(key));
    auto record = std::make_shared<assets::AssetRecord>();
    record->id = id;
    record->state = assets::AssetState::Ready;
    {
      assets::AssetHandle handle(record);
      (void)budget.Resident(id, bytes_per_chunk_, handle);
    }
    resident_.insert(key);
    ++load_count_;
  }

  const auto evicted = budget.EvictIfNeeded();
  for (const auto& id : evicted) {
    for (auto it = resident_.begin(); it != resident_.end();) {
      if (ChunkAssetId(*it) == id.value()) {
        it = resident_.erase(it);
        ++unload_count_;
      } else {
        ++it;
      }
    }
  }
}

}  // namespace engine::terrain
