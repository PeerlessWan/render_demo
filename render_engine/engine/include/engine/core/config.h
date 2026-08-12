#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine {

class Config {
 public:
  void set(std::string_view key, std::string value);
  [[nodiscard]] std::optional<std::string> get(std::string_view key) const;
  [[nodiscard]] bool has(std::string_view key) const;
  void clear();

 private:
  std::unordered_map<std::string, std::string> values_;
};

}  // namespace engine
