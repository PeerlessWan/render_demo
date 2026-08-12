#pragma once

#include <string_view>

namespace engine {

enum class FeatureLevel { L0, L1, L2 };

struct FeatureSet {
  bool d3d12 = false;
  bool vulkan = false;
  bool raytracing = false;
  bool video_decode = false;
  bool quic = false;
  bool multithread_submit = true;
  FeatureLevel level = FeatureLevel::L0;
};

FeatureSet QueryFeatures();
bool QueryFeature(std::string_view name);

}  // namespace engine
