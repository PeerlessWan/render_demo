#include "engine/assets/streaming_budget.h"

namespace engine::assets {

StreamingBudget::StreamingBudget(std::size_t budget_bytes) : budget_(budget_bytes) {}

void StreamingBudget::set_budget(std::size_t bytes) { budget_ = bytes; }

Status StreamingBudget::Resident(const AssetId& id, std::size_t size_bytes,
                                 const AssetHandle& keep_alive) {
  if (auto it = residents_.find(id); it != residents_.end()) {
    used_ -= it->second.size;
    used_ += size_bytes;
    it->second.size = size_bytes;
    it->second.handle = keep_alive;
    return Status::Ok();
  }
  Entry e;
  e.size = size_bytes;
  e.handle = keep_alive;
  residents_.emplace(id, std::move(e));
  used_ += size_bytes;
  return Status::Ok();
}

void StreamingBudget::Release(const AssetId& id) {
  auto it = residents_.find(id);
  if (it == residents_.end()) {
    return;
  }
  used_ -= it->second.size;
  residents_.erase(it);
}

std::vector<AssetId> StreamingBudget::EvictIfNeeded() {
  std::vector<AssetId> evicted;
  if (used_ <= budget_) {
    return evicted;
  }
  // Evict zero-refcount first (simple unordered scan).
  for (auto it = residents_.begin(); it != residents_.end() && used_ > budget_;) {
    // Evict when only the budget (or nothing) retains the handle.
    if (!it->second.handle.valid() || it->second.handle.refcount() <= 1) {
      used_ -= it->second.size;
      evicted.push_back(it->first);
      it = residents_.erase(it);
    } else {
      ++it;
    }
  }
  return evicted;
}

int LodSelect::SelectLevel(float distance, const std::vector<float>& ranges) {
  for (int i = 0; i < static_cast<int>(ranges.size()); ++i) {
    if (distance < ranges[static_cast<std::size_t>(i)]) {
      return i;
    }
  }
  return static_cast<int>(ranges.size());
}

}  // namespace engine::assets
