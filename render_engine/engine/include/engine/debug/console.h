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

// CPU scopes + optional GPU pass names. D3D12 timestamps live on IDevice::GpuPassBegin/End
// (CreateQueryHeap); call SetGpuTimestampAvailable(device.GpuTimestampAvailable()) after init.
// BeginPass/EndPass always record CPU nested scopes named after RenderSystem passes.
class Profiler {
 public:
  void Begin(const char* name);
  void End(const char* name);

  // Nested CPU pass scopes (pair with RenderSystem Draw path / IDevice GpuPass*).
  void BeginPass(const char* name);
  void EndPass();

  [[nodiscard]] double last_ms(const char* name) const;
  [[nodiscard]] const std::unordered_map<std::string, double>& samples_ms() const {
    return samples_ms_;
  }
  [[nodiscard]] const std::vector<std::string>& last_pass_names() const { return last_pass_names_; }

  // True when a live D3D12 device reported timestamp query heap support.
  [[nodiscard]] static bool GpuTimestampAvailable();
  static void SetGpuTimestampAvailable(bool available);

 private:
  std::unordered_map<std::string, double> open_;
  std::unordered_map<std::string, double> samples_ms_;
  std::vector<std::string> pass_stack_;
  std::vector<std::string> last_pass_names_;
};

}  // namespace engine::debug
