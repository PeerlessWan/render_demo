#include "engine/core/feature.h"

namespace engine {

FeatureSet QueryFeatures() {
  FeatureSet f;
#if defined(_WIN32)
  f.d3d12 = true;
  f.level = FeatureLevel::L1;
#endif
  f.vulkan = false;          // M17 stub until backend lands
  f.raytracing = false;      // optional; disable cleanly
  f.video_decode = false;    // D3D12VA stub reports unavailable
  f.quic = false;
  f.multithread_submit = true;
  return f;
}

bool QueryFeature(std::string_view name) {
  const FeatureSet f = QueryFeatures();
  if (name == "d3d12") return f.d3d12;
  if (name == "vulkan") return f.vulkan;
  if (name == "raytracing") return f.raytracing;
  if (name == "video_decode") return f.video_decode;
  if (name == "quic") return f.quic;
  if (name == "multithread_submit") return f.multithread_submit;
  return false;
}

}  // namespace engine
