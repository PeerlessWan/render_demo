#pragma once

#include "engine/assets/asset_id.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace engine::assets {

enum class AssetState {
  Empty,
  Pending,
  Ready,
  Failed,
  Cancelled,
};

struct AssetRecord {
  AssetId id;
  AssetState state = AssetState::Empty;
  std::string error;
  std::vector<std::uint8_t> bytes;
  int refcount = 0;
};

// Ref-counted handle; GPU fence retirement comes later (M2–M3 skeleton).
class AssetHandle {
 public:
  AssetHandle() = default;
  explicit AssetHandle(std::shared_ptr<AssetRecord> record) : record_(std::move(record)) {
    if (record_) {
      ++record_->refcount;
    }
  }

  AssetHandle(const AssetHandle& other) : record_(other.record_) {
    if (record_) {
      ++record_->refcount;
    }
  }

  AssetHandle& operator=(const AssetHandle& other) {
    if (this == &other) {
      return *this;
    }
    Release();
    record_ = other.record_;
    if (record_) {
      ++record_->refcount;
    }
    return *this;
  }

  AssetHandle(AssetHandle&& other) noexcept : record_(std::move(other.record_)) {}

  AssetHandle& operator=(AssetHandle&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    Release();
    record_ = std::move(other.record_);
    return *this;
  }

  ~AssetHandle() { Release(); }

  [[nodiscard]] bool valid() const { return static_cast<bool>(record_); }
  [[nodiscard]] explicit operator bool() const { return valid(); }
  [[nodiscard]] AssetId id() const { return record_ ? record_->id : AssetId{}; }
  [[nodiscard]] AssetState state() const {
    return record_ ? record_->state : AssetState::Empty;
  }
  [[nodiscard]] bool is_ready() const { return state() == AssetState::Ready; }
  [[nodiscard]] const std::string& error() const {
    static const std::string kEmpty;
    return record_ ? record_->error : kEmpty;
  }
  [[nodiscard]] std::span<const std::uint8_t> bytes() const {
    if (!record_ || record_->state != AssetState::Ready) {
      return {};
    }
    return record_->bytes;
  }
  [[nodiscard]] int refcount() const { return record_ ? record_->refcount : 0; }

  [[nodiscard]] std::shared_ptr<AssetRecord> record() const { return record_; }

 private:
  void Release() {
    if (record_) {
      --record_->refcount;
      record_.reset();
    }
  }

  std::shared_ptr<AssetRecord> record_;
};

}  // namespace engine::assets
