#pragma once

#include "engine/core/result.h"
#include "engine/debug/debug_draw.h"
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
#include <span>
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
  std::filesystem::path debug_vs;
  std::filesystem::path debug_ps;
  std::filesystem::path sky_vs;
  std::filesystem::path sky_ps;
  bool enable_shadows = true;
  float max_shadow_distance = 80.f;
  QualitySettings quality = QualitySettings::FromTier(QualityTier::Medium);
};

// Runtime-tunable lighting / FX knobs (Sandbox UI writes these).
struct EffectTuning {
  float sun_intensity = 4.2f;
  float ambient_scale = 1.35f;
  float shadow_bias = 0.0025f;
  float specular_power = 64.f;
  float local_intensity_scale = 1.15f;
  float exposure = 1.2f;
  bool enable_shadows = true;
  bool enable_ssao = true;
  bool enable_taa = false;
  bool enable_tonemap = true;
  bool enable_auto_exposure = false;
  bool enable_bloom = true;
  bool enable_fog = false;
  bool enable_ssr = false;
  int tonemap_mode = 2;  // 0=none 1=reinhard 2=ACES
  int shadow_cascades = 2;
  float auto_exposure_key = 0.18f;
  float bloom_threshold = 0.85f;
  float bloom_intensity = 0.4f;
  float fog_density = 0.02f;
  float fog_start = 12.f;
  Vec3 fog_color{0.62f, 0.70f, 0.78f};
  float ssr_intensity = 0.55f;
  float ssr_thickness = 0.015f;
  bool enable_dof = false;
  float dof_focus = 8.f;
  float dof_scale = 0.08f;
  bool enable_motion_blur = false;
  float motion_blur_strength = 0.35f;
  bool enable_reflection_probe = true;
  float reflection_intensity = 0.45f;
  bool enable_ibl = false;
  float ibl_intensity = 1.f;
  bool enable_skybox = true;
};

class RenderSystem {
 public:
  Status Init(rhi::IDevice& device, const RenderSystemDesc& desc);
  Status DrawFrame(rhi::IDevice& device, const RenderScene& scene, const Environment& env,
                   float aspect, const std::vector<render2d::Sprite>* sprites = nullptr,
                   const std::vector<rhi::ScreenQuad>* ui_quads = nullptr,
                   const debug::DebugDraw* debug_draw = nullptr);

  [[nodiscard]] const FrameGraph& frame_graph() const { return graph_; }
  [[nodiscard]] std::uint32_t last_draw_count() const { return last_draw_count_; }
  [[nodiscard]] bool shadows_enabled() const { return effect_.enable_shadows; }
  [[nodiscard]] int cascade_count() const { return csm_.cascade_count(); }
  [[nodiscard]] const post::PostStack& post_stack() const { return post_; }
  [[nodiscard]] const QualitySettings& quality() const { return quality_; }
  [[nodiscard]] const EffectTuning& effect_tuning() const { return effect_; }

  void set_quality(const QualitySettings& q);
  void set_effect_tuning(const EffectTuning& t);
  // Copy Environment defaults (fog/exposure/skybox flag/sun) into EffectTuning once.
  void ApplyEnvironmentDefaults(const Environment& env);
  void set_local_lights(const std::vector<LocalLight>& lights);
  void set_shadows_enabled(bool on);
  void set_post_enabled(std::string_view name, bool on);

  // Draw during OpaqueLit (before post). Avoids post-then-HDR RT DEVICE_REMOVED.
  // Worlds are also CSM-cast so scale pillars match D3D12/Vulkan contact shadows.
  void SetPendingLitInstanced(const rhi::LitDrawItem& prototype,
                              std::span<const Mat4> worlds) {
    pending_instanced_ = true;
    pending_instanced_proto_ = prototype;
    pending_instanced_worlds_.assign(worlds.begin(), worlds.end());
  }

 private:
  void ApplyEffectToQuality();

  bool ready_ = false;
  bool post_ready_ = false;
  bool sky_ready_ = false;
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
  Mat4 prev_view_proj_ = Mat4::Identity();
  bool have_prev_view_proj_ = false;
  bool pending_instanced_ = false;
  rhi::LitDrawItem pending_instanced_proto_{};
  std::vector<Mat4> pending_instanced_worlds_;
};

}  // namespace engine::render
