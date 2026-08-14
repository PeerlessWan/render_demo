#include "engine/core/feature.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace engine {
namespace {

std::mutex g_feature_mu;
std::unordered_map<std::string, bool> g_feature_overrides;

}  // namespace

FeatureSet QueryFeatures() {
  FeatureSet f;
#if defined(_WIN32)
  f.d3d12 = true;
  f.level = FeatureLevel::L1;
#endif
#if defined(ENGINE_WITH_VULKAN) && ENGINE_WITH_VULKAN
  f.vulkan = true;  // M17: Win32 clear + lit cube (no shadows)
#else
  f.vulkan = false;
#endif
  f.raytracing = false;
  f.video_decode = false;
  f.quic = false;
  f.multithread_submit = true;
  f.bindless = false;
  f.hdr_output = false;
  f.gpu_instancing = false;
  f.execute_indirect = false;
  f.hiz = false;

  std::lock_guard lock(g_feature_mu);
  auto apply = [&](const char* name, bool& dst) {
    if (auto it = g_feature_overrides.find(name); it != g_feature_overrides.end()) {
      dst = it->second;
    }
  };
  apply("bindless", f.bindless);
  apply("hdr_output", f.hdr_output);
  apply("multithread_submit", f.multithread_submit);
  apply("raytracing", f.raytracing);
  apply("video_decode", f.video_decode);
  apply("quic", f.quic);
  apply("gpu_instancing", f.gpu_instancing);
  apply("execute_indirect", f.execute_indirect);
  apply("hiz", f.hiz);
  return f;
}

bool QueryFeature(std::string_view name) {
  {
    std::lock_guard lock(g_feature_mu);
    if (auto it = g_feature_overrides.find(std::string(name)); it != g_feature_overrides.end()) {
      return it->second;
    }
  }
  const FeatureSet f = QueryFeatures();
  if (name == "d3d12") return f.d3d12;
  if (name == "vulkan") return f.vulkan;
  if (name == "raytracing") return f.raytracing;
  if (name == "video_decode") return f.video_decode;
  if (name == "quic") return f.quic;
  if (name == "multithread_submit") return f.multithread_submit;
  if (name == "bindless") return f.bindless;
  if (name == "hdr_output") return f.hdr_output;
  if (name == "gpu_instancing") return f.gpu_instancing;
  if (name == "execute_indirect") return f.execute_indirect;
  if (name == "hiz") return f.hiz;
  return false;
}

void SetFeatureOverride(std::string_view name, bool value) {
  std::lock_guard lock(g_feature_mu);
  g_feature_overrides[std::string(name)] = value;
}

void ClearFeatureOverrides() {
  std::lock_guard lock(g_feature_mu);
  g_feature_overrides.clear();
}

}  // namespace engine
