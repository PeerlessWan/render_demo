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
  // Capability: D3D12 ResourceBindingTier>=2 (ProbeBindlessMinimalPath).
  bool bindless = false;
  // Optional hot path: opaque DrawLit sets ObjectCB.pad to albedo heap slot (SM6.6
  // ResourceDescriptorHeap). Default OFF — golden/C4 stay on classic t1/t4 (pad=-1).
  // Enable via SetFeatureOverride only when bindless capable and not gpu_headless.
  bool bindless_hot_path = false;
  bool hdr_output = false;
  bool gpu_instancing = false;
  bool execute_indirect = false;
  bool hiz = false;
  FeatureLevel level = FeatureLevel::L0;
};

FeatureSet QueryFeatures();
bool QueryFeature(std::string_view name);

// Runtime capability reported by a live device (e.g. D3D12 after bindless/HDR init).
void SetFeatureOverride(std::string_view name, bool value);
void ClearFeatureOverrides();

}  // namespace engine
