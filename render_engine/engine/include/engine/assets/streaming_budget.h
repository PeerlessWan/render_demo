#pragma once

#include "engine/assets/asset_handle.h"
#include "engine/core/result.h"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

// M10: streaming memory budget cooperating with handle refcounts.
class StreamingBudget {
 public:
  explicit StreamingBudget(std::size_t budget_bytes);

  void set_budget(std::size_t bytes);
  [[nodiscard]] std::size_t budget() const { return budget_; }
  [[nodiscard]] std::size_t used() const { return used_; }

  // Resident entry. Refcount>0 entries are never evicted.
  Status Resident(const AssetId& id, std::size_t size_bytes, const AssetHandle& keep_alive);
  void Release(const AssetId& id);

  // Evict zero-refcount residents until used <= budget. Returns evicted ids.
  std::vector<AssetId> EvictIfNeeded();

 private:
  struct Entry {
    std::size_t size = 0;
    AssetHandle handle;
  };
  std::size_t budget_ = 0;
  std::size_t used_ = 0;
  std::unordered_map<AssetId, Entry, AssetIdHash> residents_;
};

struct LodSelect {
  static int SelectLevel(float distance, const std::vector<float>& ranges);
};

}  // namespace engine::assets
