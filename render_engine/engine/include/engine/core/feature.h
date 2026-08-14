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
  bool bindless = false;
  bool hdr_output = false;
  FeatureLevel level = FeatureLevel::L0;
};

FeatureSet QueryFeatures();
bool QueryFeature(std::string_view name);

// Runtime capability reported by a live device (e.g. D3D12 after bindless/HDR init).
void SetFeatureOverride(std::string_view name, bool value);
void ClearFeatureOverrides();

}  // namespace engine
