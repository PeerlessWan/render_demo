#pragma once

#include <string>
#include <utility>

namespace engine::assets {

// Stable logical name (path mapping may be versioned later).
class AssetId {
 public:
  AssetId() = default;
  explicit AssetId(std::string value) : value_(std::move(value)) {}

  [[nodiscard]] const std::string& value() const { return value_; }
  [[nodiscard]] bool empty() const { return value_.empty(); }

  friend bool operator==(const AssetId& a, const AssetId& b) {
    return a.value_ == b.value_;
  }
  friend bool operator!=(const AssetId& a, const AssetId& b) { return !(a == b); }

 private:
  std::string value_;
};

struct AssetIdHash {
  std::size_t operator()(const AssetId& id) const {
    return std::hash<std::string>{}(id.value());
  }
};

}  // namespace engine::assets
