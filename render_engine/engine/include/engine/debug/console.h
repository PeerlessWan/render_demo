#pragma once

#include "engine/core/result.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::debug {

using ConsoleHandler = std::function<Status(const std::vector<std::string>& args)>;

class Console {
 public:
  void Register(std::string_view command, ConsoleHandler handler);
  Status Execute(std::string_view line);

  static std::vector<std::string> Tokenize(std::string_view line);

 private:
  std::unordered_map<std::string, ConsoleHandler> handlers_;
};

struct ProfileScope {
  const char* name = "";
  double start_seconds = 0.0;
};

class Profiler {
 public:
  void Begin(const char* name);
  void End(const char* name);
  [[nodiscard]] double last_ms(const char* name) const;
  [[nodiscard]] const std::unordered_map<std::string, double>& samples_ms() const {
    return samples_ms_;
  }

 private:
  std::unordered_map<std::string, double> open_;
  std::unordered_map<std::string, double> samples_ms_;
};

}  // namespace engine::debug
