#pragma once

#include "engine/render/quality.h"

#include <string>
#include <string_view>
#include <vector>

namespace engine::post {

struct PostPassDesc {
  std::string name;
  bool enabled = true;
};

class PostStack {
 public:
  void Configure(const render::QualitySettings& q);
  void set_enabled(std::string_view name, bool on);
  [[nodiscard]] bool enabled(std::string_view name) const;
  [[nodiscard]] const std::vector<PostPassDesc>& passes() const { return passes_; }
  [[nodiscard]] std::vector<std::string> EnabledPassNames() const;

 private:
  std::vector<PostPassDesc> passes_;
};

}  // namespace engine::post
