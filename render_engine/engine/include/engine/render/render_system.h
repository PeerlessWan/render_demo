#pragma once

#include "engine/core/result.h"
#include "engine/post/post_stack.h"
#include "engine/render/camera.h"
#include "engine/render/environment.h"
#include "engine/render/frame_graph.h"
#include "engine/render/local_lights.h"
#include "engine/render/quality.h"
#include "engine/render/render_scene.h"
#include "engine/render/shadow_atlas.h"
#include "engine/render/shadow_csm.h"
#include "engine/rhi/i_device.h"
#include "engine/render2d/sprite.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace engine::render {

struct RenderSystemDesc {
  std::filesystem::path lit_vs;
  std::filesystem::path lit_ps;
  std::filesystem::path shadow_vs;
  std::filesystem::path shadow_ps;
  std::filesystem::path quad_vs;
  std::filesystem::path quad_ps;
  std::filesystem::path post_vs;
  std::filesystem::path post_ps;
  bool enable_shadows = true;
  float max_shadow_distance = 80.f;
  QualitySettings quality = QualitySettings::FromTier(QualityTier::Medium);
};

// Runtime-tunable lighting / FX knobs (Sandbox UI writes these).
struct EffectTuning {
  float sun_intensity = 2.8f;
  float ambient_scale = 1.f;
  float shadow_bias = 0.0015f;
  float specular_power = 64.f;
  float local_intensity_scale = 1.f;
  bool enable_shadows = true;
  bool enable_ssao = true;
  bool enable_taa = true;
  int shadow_cascades = 2;
};

class RenderSystem {
 public:
  Status Init(rhi::IDevice& device, const RenderSystemDesc& desc);
  Status DrawFrame(rhi::IDevice& device, const RenderScene& scene, const Environment& env,
                   float aspect, const std::vector<render2d::Sprite>* sprites = nullptr,
                   const std::vector<rhi::ScreenQuad>* ui_quads = nullptr);

  [[nodiscard]] const FrameGraph& frame_graph() const { return graph_; }
  [[nodiscard]] std::uint32_t last_draw_count() const { return last_draw_count_; }
  [[nodiscard]] bool shadows_enabled() const { return effect_.enable_shadows; }
  [[nodiscard]] int cascade_count() const { return csm_.cascade_count(); }
  [[nodiscard]] const post::PostStack& post_stack() const { return post_; }
  [[nodiscard]] const QualitySettings& quality() const { return quality_; }
  [[nodiscard]] const EffectTuning& effect_tuning() const { return effect_; }

  void set_quality(const QualitySettings& q);
  void set_effect_tuning(const EffectTuning& t);
  void set_local_lights(const std::vector<LocalLight>& lights);
  void set_shadows_enabled(bool on);
  void set_post_enabled(std::string_view name, bool on);

 private:
  void ApplyEffectToQuality();

  bool ready_ = false;
  float max_shadow_distance_ = 80.f;
  std::uint32_t frame_index_ = 0;
  QualitySettings quality_{};
  EffectTuning effect_{};
  post::PostStack post_;
  FrameGraph graph_;
  std::uint32_t last_draw_count_ = 0;
  CascadedShadowMap csm_;
  ShadowAtlas atlas_{2048};
  LocalLightShadowScheduler local_shadows_;
  std::vector<LocalLight> local_lights_;
};

}  // namespace engine::render
