#include "engine/render/render_system.h"

#include "engine/core/log.h"
#include "engine/debug/console.h"
#include "engine/render/local_lights.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <vector>

namespace engine::render {

void RenderSystem::ApplyEffectToQuality() {
  quality_.enable_ssao = effect_.enable_ssao;
  quality_.enable_taa = effect_.enable_taa;
  quality_.enable_bloom = effect_.enable_bloom;
  quality_.enable_ssr = effect_.enable_ssr;
  quality_.shadow_cascades = effect_.shadow_cascades;
  post_.Configure(quality_);
  post_.set_enabled("SSAO", effect_.enable_ssao);
  post_.set_enabled("TAA", effect_.enable_taa);
  post_.set_enabled("Bloom", effect_.enable_bloom);
  post_.set_enabled("Tonemap", effect_.enable_tonemap);
  post_.set_enabled("AutoExposure", effect_.enable_auto_exposure);
  post_.set_enabled("VolumetricFog", effect_.enable_fog);
  post_.set_enabled("SSR", effect_.enable_ssr);
  post_.set_enabled("DoF", effect_.enable_dof);
  post_.set_enabled("MotionBlur", effect_.enable_motion_blur);
  csm_.set_cascade_count(effect_.shadow_cascades);
}

void RenderSystem::set_quality(const QualitySettings& q) {
  quality_ = q;
  effect_.enable_ssao = q.enable_ssao;
  effect_.enable_taa = q.enable_taa;
  effect_.enable_bloom = q.enable_bloom;
  effect_.enable_ssr = q.enable_ssr;
  effect_.enable_dof = q.enable_dof;
  effect_.enable_motion_blur = q.enable_motion_blur;
  effect_.shadow_cascades = q.shadow_cascades;
  max_shadow_distance_ = q.max_shadow_distance;
  if (q.shadow_atlas_size > 0 && q.shadow_atlas_size != atlas_.size()) {
    atlas_.set_size(q.shadow_atlas_size);
  }
  ApplyEffectToQuality();
}

void RenderSystem::set_effect_tuning(const EffectTuning& t) {
  effect_ = t;
  if (effect_.shadow_cascades < 1) {
    effect_.shadow_cascades = 1;
  }
  if (effect_.shadow_cascades > 4) {
    effect_.shadow_cascades = 4;
  }
  if (effect_.tonemap_mode < 0) {
    effect_.tonemap_mode = 0;
  }
  if (effect_.tonemap_mode > 2) {
    effect_.tonemap_mode = 2;
  }
  ApplyEffectToQuality();
}

void RenderSystem::ApplyEnvironmentDefaults(const Environment& env) {
  effect_.sun_intensity = env.sun_intensity;
  effect_.exposure = env.exposure;
  effect_.enable_fog = env.fog_enabled;
  effect_.fog_density = env.fog_density;
  effect_.fog_start = env.fog_start;
  effect_.fog_color = {env.fog_color.r, env.fog_color.g, env.fog_color.b};
  effect_.enable_skybox = env.skybox_enabled;
  ApplyEffectToQuality();
}

void RenderSystem::set_local_lights(const std::vector<LocalLight>& lights) {
  local_lights_ = lights;
}

void RenderSystem::set_shadows_enabled(bool on) { effect_.enable_shadows = on; }

void RenderSystem::set_post_enabled(std::string_view name, bool on) {
  post_.set_enabled(name, on);
  if (name == "SSAO") {
    effect_.enable_ssao = on;
    quality_.enable_ssao = on;
  } else if (name == "TAA") {
    effect_.enable_taa = on;
    quality_.enable_taa = on;
  } else if (name == "Bloom") {
    effect_.enable_bloom = on;
    quality_.enable_bloom = on;
  } else if (name == "Tonemap") {
    effect_.enable_tonemap = on;
  } else if (name == "AutoExposure") {
    effect_.enable_auto_exposure = on;
  } else if (name == "VolumetricFog") {
    effect_.enable_fog = on;
  } else if (name == "SSR") {
    effect_.enable_ssr = on;
    quality_.enable_ssr = on;
  } else if (name == "DoF") {
    effect_.enable_dof = on;
  } else if (name == "MotionBlur") {
    effect_.enable_motion_blur = on;
  }
}

Status RenderSystem::Init(rhi::IDevice& device, const RenderSystemDesc& desc) {
  rhi::LitMeshShaders shaders;
  shaders.vs_dxil = desc.lit_vs;
  shaders.ps_dxil = desc.lit_ps;
  shaders.shadow_vs_dxil = desc.shadow_vs;
  shaders.shadow_ps_dxil = desc.shadow_ps;
  shaders.quad_vs_dxil = desc.quad_vs;
  shaders.quad_ps_dxil = desc.quad_ps;
  shaders.debug_vs_dxil = desc.debug_vs;
  shaders.debug_ps_dxil = desc.debug_ps;
  if (auto st = device.SetupLitMesh(shaders); !st) {
    return st;
  }
  if (!desc.post_vs.empty() && !desc.post_ps.empty()) {
    rhi::PostShaders post_shaders;
    post_shaders.vs_dxil = desc.post_vs;
    post_shaders.ps_dxil = desc.post_ps;
    if (auto st = device.SetupPostMesh(post_shaders); !st) {
      return st;
    }
    post_ready_ = true;
    LogInfo("Post mesh ready (SSAO/TAA/tonemap/bloom/fog)");
  } else {
    post_ready_ = false;
  }
  if (!desc.sky_vs.empty() && !desc.sky_ps.empty()) {
    if (auto st = device.SetupSkybox(desc.sky_vs, desc.sky_ps); !st) {
      LogWarn("SetupSkybox SKIP: " + st.message());
    } else {
      sky_ready_ = true;
      LogInfo("Skybox path ready");
    }
  }
  max_shadow_distance_ = desc.max_shadow_distance;
  effect_.enable_shadows = desc.enable_shadows;
  effect_.sun_intensity = 4.2f;
  set_quality(desc.quality);
  ready_ = true;
  LogInfo(effect_.enable_shadows ? "RenderSystem ready (lit+CSM)" : "RenderSystem ready (lit)");
  return Status::Ok();
}

Status RenderSystem::DrawFrame(rhi::IDevice& device, const RenderScene& scene,
                               const Environment& env, float aspect,
                               const std::vector<render2d::Sprite>* sprites,
                               const std::vector<rhi::ScreenQuad>* ui_quads,
                               const debug::DebugDraw* debug_draw) {
  if (!ready_) {
    return Status::Fail("RenderSystem not initialized");
  }
  ++frame_index_;

  std::vector<rhi::LitDrawItem> opaque;
  std::vector<rhi::LitDrawItem> transparent;
  opaque.reserve(scene.instances.size());
  for (const auto& inst : scene.instances) {
    rhi::LitDrawItem item;
    item.world = inst.world;
    const auto mat = ResolveMeshMaterial(inst.mesh_id);
    item.color = mat.base_color;
    item.roughness = mat.roughness;
    item.metallic = mat.metallic;
    item.use_albedo = !mat.albedo_tex.empty();
    item.use_orm = !mat.orm_tex.empty();
    item.transparent = mat.transparent;
    item.mesh_slot = mat.mesh_slot;
    item.tex_slot = mat.tex_slot;
    item.uv_scale = mat.uv_scale > 0.f ? mat.uv_scale : 1.f;
    if (mat.transparent) {
      transparent.push_back(item);
    } else {
      opaque.push_back(item);
    }
  }
  // Back-to-front for alpha blend (camera distance).
  std::sort(transparent.begin(), transparent.end(), [&](const rhi::LitDrawItem& a,
                                                        const rhi::LitDrawItem& b) {
    const Vec3 pa{a.world.m[12], a.world.m[13], a.world.m[14]};
    const Vec3 pb{b.world.m[12], b.world.m[13], b.world.m[14]};
    const float da = (pa - scene.camera.position).length_squared();
    const float db = (pb - scene.camera.position).length_squared();
    return da > db;
  });

  const Quat cam_q = Quat::FromEulerYxz(scene.camera.yaw, scene.camera.pitch, 0.f);
  const Vec3 cam_forward = cam_q.Rotate(Vec3{0.f, 0.f, -1.f});

  csm_.Build(scene.camera, aspect, env.sun_direction, atlas_.size(), max_shadow_distance_);
  atlas_.Reset();
  for (int i = 0; i < static_cast<int>(csm_.cascades().size()); ++i) {
    ShadowAtlasSlot slot;
    const auto& c = csm_.cascades()[static_cast<std::size_t>(i)];
    if (atlas_.Allocate(1000 + i, c.atlas_w, slot)) {
      (void)slot;
    }
  }
  local_shadows_.Clear();
  for (const auto& light : local_lights_) {
    local_shadows_.AddLight(light);
  }
  ShadowAtlas local_atlas(atlas_.size());
  (void)local_shadows_.Pack(local_atlas);

  rhi::FrameLighting lighting;
  lighting.view_proj = scene.camera.view_proj_matrix(aspect);
  lighting.prev_view_proj = have_prev_view_proj_ ? prev_view_proj_ : lighting.view_proj;
  const bool want_ssao = effect_.enable_ssao && post_.enabled("SSAO");
  const bool want_taa = effect_.enable_taa && post_.enabled("TAA");
  if (want_taa) {
    // Halton(2,3) sub-pixel jitter in NDC.
    auto halton = [](int index, int base) {
      float f = 1.f;
      float r = 0.f;
      int i = index;
      while (i > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
      }
      return r;
    };
    const int sample = static_cast<int>(frame_index_ % 8u) + 1;
    const float w = static_cast<float>((std::max)(1u, device.width()));
    const float h = static_cast<float>((std::max)(1u, device.height()));
    lighting.jitter_x = (halton(sample, 2) * 2.f - 1.f) / w;
    lighting.jitter_y = (halton(sample, 3) * 2.f - 1.f) / h;
  }
  lighting.sun_direction = Normalize(env.sun_direction);
  lighting.sun_intensity = effect_.sun_intensity;
  lighting.ambient = {env.ambient.r * effect_.ambient_scale, env.ambient.g * effect_.ambient_scale,
                       env.ambient.b * effect_.ambient_scale, env.ambient.a};
  lighting.sun_color = env.sun_color;
  lighting.eye = scene.camera.position;
  lighting.camera_forward = cam_forward;
  lighting.shadow_bias = effect_.shadow_bias;
  lighting.specular_power = effect_.specular_power;
  lighting.enable_shadows = effect_.enable_shadows;
  lighting.cascade_count = static_cast<int>(csm_.cascades().size());
  lighting.cascade_tiles_per_row = csm_.tiles_per_row();
  for (std::size_t i = 0; i < csm_.cascades().size() && i < 4; ++i) {
    lighting.cascade_view_proj[i] = csm_.cascades()[i].view_proj;
    lighting.cascade_splits[i] = csm_.cascades()[i].split;
  }
  if (!csm_.cascades().empty()) {
    lighting.light_view_proj = csm_.cascades()[0].view_proj;
  }
  lighting.local_light_count = 0;
  lighting.enable_local_shadow = false;
  lighting.local_shadow_count = 0;
  lighting.local_shadow_tile_count = 0;
  lighting.local_shadow_tiles_per_row = 4;
  lighting.local_spot = {};
  lighting.local_spot_inner.fill(-1.f);
  lighting.local_ies.fill(0.f);
  // M26/C02: accept up to 16 CPU lights; upload closest 16 (shadow casters preferred).
  struct RankedLight {
    LocalLight light;
    float dist2 = 0.f;
  };
  std::vector<RankedLight> ranked;
  ranked.reserve(local_lights_.size());
  for (const auto& light : local_lights_) {
    if (ranked.size() >= static_cast<std::size_t>(kMaxLocalLightsCpu)) {
      break;
    }
    const Vec3 d = light.position - lighting.eye;
    ranked.push_back({light, d.length_squared()});
  }
  std::sort(ranked.begin(), ranked.end(), [](const RankedLight& a, const RankedLight& b) {
    if (a.light.cast_shadows != b.light.cast_shadows) {
      return a.light.cast_shadows && !b.light.cast_shadows;
    }
    return a.dist2 < b.dist2;
  });
  std::vector<LocalLight> packed_lights;
  packed_lights.reserve(static_cast<std::size_t>(kMaxLocalLightsGpu));
  for (const auto& r : ranked) {
    if (lighting.local_light_count >= kMaxLocalLightsGpu) {
      break;
    }
    const auto& light = r.light;
    const int i = lighting.local_light_count++;
    packed_lights.push_back(light);
    lighting.local_pos[static_cast<std::size_t>(i)] = light.position;
    lighting.local_range[static_cast<std::size_t>(i)] = light.range;
    lighting.local_color[static_cast<std::size_t>(i)] = light.color;
    lighting.local_intensity[static_cast<std::size_t>(i)] =
        light.intensity * effect_.local_intensity_scale;
    constexpr float kDegToRad = 0.01745329252f;
    Vec3 dir = light.direction;
    if (dir.length_squared() < 1e-6f) {
      dir = Vec3{0.f, -1.f, 0.f};
    }
    dir = Normalize(dir);
    float cos_outer = -1.f;
    float cos_inner = -1.f;
    if (IsSpotLight(light)) {
      const float outer = std::max(light.spot_angle_deg, 0.5f) * kDegToRad;
      float inner = std::max(light.spot_inner_deg, 0.f) * kDegToRad;
      if (inner > outer) {
        inner = outer;
      }
      cos_outer = std::cos(outer);
      cos_inner = std::cos(inner);
    }
    lighting.local_spot[static_cast<std::size_t>(i)] = {dir.x, dir.y, dir.z, cos_outer};
    lighting.local_spot_inner[static_cast<std::size_t>(i)] = cos_inner;
    lighting.local_ies[static_cast<std::size_t>(i)] =
        static_cast<float>(std::max(0, light.ies_profile));
  }
  if (effect_.enable_shadows) {
    // Up to 2 cubemap/spot lights (12 faces) fit in the 2048 atlas as 4×4 × 512 tiles.
    // Matrices are stored at light_index * 6 + face (matches lit_cube.hlsl).
    // Spot lights write the same perspective VP into all 6 face slots (shader uses face 0).
    // Remaining uploaded lights (indices ≥ shadow count) are unshadowed in the shader.
    for (int i = 0; i < lighting.local_light_count && i < kMaxLocalShadowLights; ++i) {
      const auto& light = packed_lights[static_cast<std::size_t>(i)];
      if (!light.cast_shadows) {
        continue;
      }
      if (IsSpotLight(light)) {
        const Mat4 spot_vp = BuildLocalShadowMatrix(light);
        for (int f = 0; f < 6; ++f) {
          lighting.local_shadow_vps[static_cast<std::size_t>(i * 6 + f)] = spot_vp;
        }
      } else {
        const auto faces = BuildLocalShadowCubeMatrices(light);
        for (int f = 0; f < 6; ++f) {
          lighting.local_shadow_vps[static_cast<std::size_t>(i * 6 + f)] =
              faces[static_cast<std::size_t>(f)];
        }
      }
      lighting.local_shadow_count = i + 1;
      lighting.enable_local_shadow = true;
    }
    if (lighting.enable_local_shadow) {
      lighting.local_shadow_tile_count = lighting.local_shadow_count * 6;
      lighting.local_shadow_vp = lighting.local_shadow_vps[0];
      lighting.local_shadow_bias = effect_.shadow_bias * 1.25f;
      lighting.local_shadow_tiles_per_row = 4;
    }
  }
  // Real SSAO/TAA run in ResolvePostEffects; avoid double-applying lit approximate path.
  lighting.enable_ssao = false;
  lighting.enable_taa = want_taa;  // mild lit hint only when TAA on
  lighting.enable_reflection_probe = effect_.enable_reflection_probe;
  lighting.reflection_intensity = effect_.reflection_intensity;
  lighting.enable_ibl = effect_.enable_ibl && env.has_ibl();
  lighting.ibl_intensity = effect_.ibl_intensity;

  // Mega-W8 C02: prefer GPU Feature path (DispatchLightTileCull / Simulate) else CPU Assign.
  lighting.enable_tiled_lights = effect_.enable_tiled_lights;
  lighting.tile_light_count.fill(0);
  lighting.tile_light_index.fill(-1);
  if (lighting.enable_tiled_lights && lighting.local_light_count > 0) {
    std::array<int, kLightTileCount> counts{};
    std::array<int, kTileLightIndexCount> indices{};
    const std::span<const Vec3> positions(lighting.local_pos.data(),
                                          static_cast<std::size_t>(lighting.local_light_count));
    const std::span<const float> ranges(lighting.local_range.data(),
                                        static_cast<std::size_t>(lighting.local_light_count));
    bool filled = false;
    if (auto st = device.DispatchLightTileCull(lighting.view_proj, positions, ranges, counts,
                                               indices);
        st) {
      filled = true;
    }
    if (!filled) {
      std::vector<std::vector<int>> tiles;
      AssignLightsToTiles(packed_lights, lighting.view_proj, kLightTileGridW, kLightTileGridH,
                          tiles);
      PackTileLightLists(tiles, counts, indices);
    }
    for (int t = 0; t < kLightTileCount; ++t) {
      lighting.tile_light_count[static_cast<std::size_t>(t)] = counts[static_cast<std::size_t>(t)];
    }
    for (int i = 0; i < kTileLightIndexCount; ++i) {
      lighting.tile_light_index[static_cast<std::size_t>(i)] = indices[static_cast<std::size_t>(i)];
    }
  }

  if (quality_.multithread_submit) {
    rhi::SubmitConfig cfg;
    cfg.multithread = true;
    cfg.worker_count = 2;
    (void)device.SetSubmitConfig(cfg);
  }

  std::vector<rhi::ScreenQuad> quads;
  if (sprites) {
    for (const auto& s : *sprites) {
      rhi::ScreenQuad q;
      q.x0 = s.position.x;
      q.y0 = s.position.y;
      q.x1 = s.position.x + s.size.x;
      q.y1 = s.position.y + s.size.y;
      q.color = s.color;
      quads.push_back(q);
    }
  }
  if (ui_quads) {
    quads.insert(quads.end(), ui_quads->begin(), ui_quads->end());
  }

  graph_.Reset();
  auto pass_begin = [&](const char* name) {
    if (profiler_) {
      profiler_->BeginPass(name);
    }
    device.GpuPassBegin(name);
  };
  auto pass_end = [&]() {
    device.GpuPassEnd();
    if (profiler_) {
      profiler_->EndPass();
    }
  };
  if (effect_.enable_shadows) {
    graph_.AddPass("ShadowCSM", {}, {"ShadowMap"}, [&] {
      pass_begin("ShadowCSM");
      if (auto st = device.BeginShadowPass(); !st) {
        LogError(st.message());
        pass_end();
        return;
      }
      if (auto st = device.SetFrameLighting(lighting); !st) {
        LogError(st.message());
      }
      for (int i = 0; i < lighting.cascade_count; ++i) {
        if (auto st = device.BindShadowCascade(i); !st) {
          LogError(st.message());
          break;
        }
        if (auto st = device.DrawShadowCubes(opaque); !st) {
          LogError(st.message());
          break;
        }
        if (pending_instanced_ && !pending_instanced_worlds_.empty()) {
          std::vector<rhi::LitDrawItem> shadow_inst;
          shadow_inst.resize(pending_instanced_worlds_.size(), pending_instanced_proto_);
          for (std::size_t si = 0; si < pending_instanced_worlds_.size(); ++si) {
            shadow_inst[si].world = pending_instanced_worlds_[si];
          }
          if (auto st = device.DrawShadowCubes(shadow_inst); !st) {
            LogError(st.message());
            break;
          }
        }
      }
      if (auto st = device.EndShadowPass(); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  if (lighting.enable_local_shadow) {
    graph_.AddPass("LocalShadow", {}, {"LocalShadowMap"}, [&] {
      pass_begin("LocalShadow");
      if (auto st = device.SetFrameLighting(lighting); !st) {
        LogError(st.message());
      }
      if (auto st = device.BeginLocalShadowPass(); !st) {
        LogError(st.message());
        pass_end();
        return;
      }
      for (int i = 0; i < lighting.local_shadow_tile_count; ++i) {
        if (auto st = device.BindLocalShadowTile(i); !st) {
          LogError(st.message());
          break;
        }
        if (auto st = device.DrawShadowCubes(opaque); !st) {
          LogError(st.message());
          break;
        }
      }
      if (auto st = device.EndLocalShadowPass(); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  graph_.AddPass(
      "OpaqueLit",
      [&] {
        std::vector<std::string> reads;
        if (effect_.enable_shadows) {
          reads.push_back("ShadowMap");
        }
        if (lighting.enable_local_shadow) {
          reads.push_back("LocalShadowMap");
        }
        return reads;
      }(),
      {"Color", "Depth"},
      [&] {
        pass_begin("Opaque");
        if (auto st = device.SetFrameLighting(lighting); !st) {
          LogError(st.message());
          pass_end();
          return;
        }
        if (auto st = device.DrawLitCubes(opaque); !st) {
          LogError(st.message());
        }
        if (pending_instanced_ && !pending_instanced_worlds_.empty()) {
          if (auto st = device.DrawLitInstanced(pending_instanced_proto_,
                                               static_cast<std::uint32_t>(
                                                   pending_instanced_worlds_.size()));
              !st) {
            LogError(st.message());
          }
          pending_instanced_ = false;
          pending_instanced_worlds_.clear();
        }
        pass_end();
      });
  if (!transparent.empty()) {
    graph_.AddPass("Transparent", {"Color", "Depth"}, {"Color"}, [&] {
      pass_begin("Transparent");
      if (auto st = device.SetFrameLighting(lighting); !st) {
        LogError(st.message());
        pass_end();
        return;
      }
      if (auto st = device.DrawTransparentLitCubes(transparent); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  if (sky_ready_ && effect_.enable_skybox) {
    graph_.AddPass("Skybox", {"Color", "Depth"}, {"Color"}, [&] {
      pass_begin("Skybox");
      Mat4 view_rot = scene.camera.view_matrix();
      view_rot.m[12] = 0.f;
      view_rot.m[13] = 0.f;
      view_rot.m[14] = 0.f;
      const Mat4 sky_vp = scene.camera.proj_matrix(aspect) * view_rot;
      if (auto st = device.DrawSkybox(sky_vp); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  const bool want_tonemap = true;  // HDR scene color always tonemaps to LDR swapchain
  const bool want_auto_exp = effect_.enable_auto_exposure && post_.enabled("AutoExposure");
  const bool want_bloom = effect_.enable_bloom && post_.enabled("Bloom");
  const bool want_fog = effect_.enable_fog && post_.enabled("VolumetricFog");
  const bool want_ssr = effect_.enable_ssr && post_.enabled("SSR");
  const bool want_dof = effect_.enable_dof && post_.enabled("DoF");
  const bool want_motion_blur = effect_.enable_motion_blur && post_.enabled("MotionBlur");
  (void)want_tonemap;
  // HDR lit target must always be resolved (at least tonemap) into the LDR swapchain.
  if (post_ready_) {
    graph_.AddPass("PostSSAO_TAA", {"Color", "Depth"}, {"Color"}, [&] {
      pass_begin("Post");
      rhi::PostResolveDesc post;
      post.view_proj = lighting.view_proj;
      post.inv_view_proj = lighting.view_proj.Inverse();
      post.eye = lighting.eye;
      post.enable_ssao = want_ssao;
      post.enable_taa = want_taa;
      post.exposure = effect_.exposure;
      post.enable_tonemap = true;
      post.tonemap_mode = effect_.tonemap_mode;
      post.enable_auto_exposure = want_auto_exp;
      post.auto_exposure_key = effect_.auto_exposure_key;
      post.enable_bloom = want_bloom;
      post.bloom_threshold = effect_.bloom_threshold;
      post.bloom_intensity = effect_.bloom_intensity;
      post.enable_fog = want_fog;
      post.fog_density = effect_.fog_density;
      post.fog_start = effect_.fog_start;
      post.fog_color = effect_.fog_color;
      post.enable_ssr = want_ssr;
      post.ssr_intensity = effect_.ssr_intensity;
      post.ssr_thickness = effect_.ssr_thickness;
      post.enable_dof = want_dof;
      post.dof_focus = effect_.dof_focus;
      post.dof_scale = effect_.dof_scale;
      post.enable_motion_blur = want_motion_blur;
      post.motion_blur_strength = effect_.motion_blur_strength;
      post.prev_view_proj = lighting.prev_view_proj;
      post.jitter_x = lighting.jitter_x;
      post.jitter_y = lighting.jitter_y;
      post.vignette_strength = effect_.vignette_strength;
      post.film_grain_strength = effect_.film_grain_strength;
      post.chromatic_aberration = effect_.chromatic_aberration;
      post.lens_distortion = effect_.lens_distortion;
      post.light_dirt_strength = effect_.light_dirt_strength;
      post.flare_strength = effect_.flare_strength;
      if (auto st = device.ResolvePostEffects(post); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  if (debug_draw && !debug_draw->lines().empty()) {
    graph_.AddPass("DebugLines", {"Color", "Depth"}, {"Color"}, [&] {
      pass_begin("Debug");
      std::vector<rhi::DebugLineVertex> verts;
      verts.reserve(debug_draw->lines().size() * 2);
      for (const auto& ln : debug_draw->lines()) {
        verts.push_back({ln.a.x, ln.a.y, ln.a.z, ln.color.r, ln.color.g, ln.color.b, ln.color.a});
        verts.push_back({ln.b.x, ln.b.y, ln.b.z, ln.color.r, ln.color.g, ln.color.b, ln.color.a});
      }
      if (auto st = device.DrawDebugLines(verts); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }
  if (!quads.empty()) {
    graph_.AddPass("UI2D", {"Color"}, {"Color"}, [&] {
      pass_begin("UI");
      if (auto st = device.DrawScreenQuads(quads); !st) {
        LogError(st.message());
      }
      pass_end();
    });
  }

  if (auto st = graph_.Compile(); !st) {
    return st;
  }
  if (auto st = graph_.Execute(); !st) {
    return st;
  }
  prev_view_proj_ = lighting.view_proj;
  have_prev_view_proj_ = true;
  last_draw_count_ = static_cast<std::uint32_t>(opaque.size() + transparent.size());
  return Status::Ok();
}

}  // namespace engine::render
