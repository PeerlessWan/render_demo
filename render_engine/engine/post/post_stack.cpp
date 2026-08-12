#include "engine/post/post_stack.h"

namespace engine::post {

void PostStack::Configure(const render::QualitySettings& q) {
  passes_ = {
      {"Tonemap", true},
      {"Bloom", q.enable_bloom},
      {"TAA", q.enable_taa},
      {"SSAO", q.enable_ssao},
      {"SSR", q.enable_ssr},
      {"DoF", false},
      {"MotionBlur", false},
      {"AutoExposure", true},
      {"VolumetricFog", false},
  };
}

void PostStack::set_enabled(std::string_view name, bool on) {
  for (auto& p : passes_) {
    if (p.name == name) {
      p.enabled = on;
    }
  }
}

bool PostStack::enabled(std::string_view name) const {
  for (const auto& p : passes_) {
    if (p.name == name) {
      return p.enabled;
    }
  }
  return false;
}

std::vector<std::string> PostStack::EnabledPassNames() const {
  std::vector<std::string> out;
  for (const auto& p : passes_) {
    if (p.enabled) {
      out.push_back(p.name);
    }
  }
  return out;
}

}  // namespace engine::post
