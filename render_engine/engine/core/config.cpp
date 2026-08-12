#include "engine/core/config.h"

namespace engine {

void Config::set(std::string_view key, std::string value) {
  values_[std::string(key)] = std::move(value);
}

std::optional<std::string> Config::get(std::string_view key) const {
  const auto it = values_.find(std::string(key));
  if (it == values_.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool Config::has(std::string_view key) const {
  return values_.find(std::string(key)) != values_.end();
}

void Config::clear() { values_.clear(); }

}  // namespace engine
