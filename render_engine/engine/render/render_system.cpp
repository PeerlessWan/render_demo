#include "engine/render/render_system.h"

#include "engine/core/log.h"
#include "engine/render/local_lights.h"

#include <algorithm>
#include <string>

namespace engine::render {

void RenderSystem::ApplyEffectToQuality() {
  quality_.enable_ssao = effect_.enable_ssao;
  quality_.enable_taa = effect_.enable_taa;
  quality_.shadow_cascades = effect_.shadow_cascades;
  post_.Configure(quality_);
  post_.set_enabled("SSAO", effect_.enable_ssao);
  post_.set_enabled("TAA", effect_.enable_taa);
  csm_.set_cascade_count(effect_.shadow_cascades);
}

void RenderSystem::set_quality(const QualitySettings& q) {
  quality_ = q;
  effect_.enable_ssao = q.enable_ssao;
  effect_.enable_taa = q.enable_taa;
  effect_.shadow_cascades = q.shadow_cascades;
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
    LogInfo("Post mesh ready (SSAO+TAA resolve)");
  }
  max_shadow_distance_ = desc.max_shadow_distance;
  effect_.enable_shadows = desc.enable_shadows;
  effect_.sun_intensity = 2.8f;
  set_quality(desc.quality);
  ready_ = true;
  LogInfo(effect_.enable_shadows ? "RenderSystem ready (lit+CSM)" : "RenderSystem ready (lit)");
  return Status::Ok();
}

Status RenderSystem::DrawFrame(rhi::IDevice& device, const RenderScene& scene,
                               const Environment& env, float aspect,
                               const std::vector<render2d::Sprite>* sprites,
                               const std::vector<rhi::ScreenQuad>* ui_quads) {
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
  const bool want_ssao = effect_.enable_ssao && post_.enabled("SSAO");
  const bool want_taa = effect_.enable_taa && post_.enabled("TAA");
  if (want_taa) {
    Mat4 jitter = Mat4::Identity();
    const float jx = ((frame_index_ & 1u) ? 0.4f : -0.4f) /
                     static_cast<float>((std::max)(device.width(), 1u));
    const float jy = ((frame_index_ & 2u) ? 0.4f : -0.4f) /
                     static_cast<float>((std::max)(device.height(), 1u));
    jitter.m[12] = jx;
    jitter.m[13] = jy;
    lighting.view_proj = jitter * lighting.view_proj;
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
  lighting.local_shadow_tiles_per_row = 2;
  for (const auto& light : local_lights_) {
    if (lighting.local_light_count >= 4) {
      break;
    }
    const int i = lighting.local_light_count++;
    lighting.local_pos[static_cast<std::size_t>(i)] = light.position;
    lighting.local_range[static_cast<std::size_t>(i)] = light.range;
    lighting.local_color[static_cast<std::size_t>(i)] = light.color;
    lighting.local_intensity[static_cast<std::size_t>(i)] =
        light.intensity * effect_.local_intensity_scale;
  }
  if (effect_.enable_shadows) {
    for (int i = 0; i < lighting.local_light_count && lighting.local_shadow_count < 4; ++i) {
      const auto& light = local_lights_[static_cast<std::size_t>(i)];
      if (!light.cast_shadows) {
        continue;
      }
      const int tile = lighting.local_shadow_count++;
      lighting.local_shadow_vps[static_cast<std::size_t>(tile)] = BuildLocalShadowMatrix(light);
    }
    if (lighting.local_shadow_count > 0) {
      lighting.enable_local_shadow = true;
      lighting.local_shadow_vp = lighting.local_shadow_vps[0];
      lighting.local_shadow_bias = effect_.shadow_bias * 1.25f;
      lighting.local_shadow_tiles_per_row =
          lighting.local_shadow_count > 1 ? 2 : 1;
    }
  }
  // Real SSAO/TAA run in ResolvePostEffects; avoid double-applying lit approximate path.
  lighting.enable_ssao = false;
  lighting.enable_taa = want_taa;  // mild lit hint only when TAA on

  std::vector<rhi::ScreenQuad> quads;
  if (sprites) {
    for (const auto& s : *sprites) {
      rhi::ScreenQuad q;
      q.x0 = s.position.x;
      q.y0 = s.position.y;
      q.x1 = s.position.x + s.size.x;
      q.y1 = s.position.y + s.size.y;
      q.color = {0.2f, 0.85f, 0.35f, 0.9f};
      quads.push_back(q);
    }
  }
  if (ui_quads) {
    quads.insert(quads.end(), ui_quads->begin(), ui_quads->end());
  }

  graph_.Reset();
  if (effect_.enable_shadows) {
    graph_.AddPass("ShadowCSM", {}, {"ShadowMap"}, [&] {
      device.GpuPassBegin("ShadowCSM");
      if (auto st = device.BeginShadowPass(); !st) {
        LogError(st.message());
        device.GpuPassEnd();
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
      }
      if (auto st = device.EndShadowPass(); !st) {
        LogError(st.message());
      }
      device.GpuPassEnd();
    });
  }
  if (lighting.enable_local_shadow) {
    graph_.AddPass("LocalShadow", {}, {"LocalShadowMap"}, [&] {
      device.GpuPassBegin("LocalShadow");
      if (auto st = device.SetFrameLighting(lighting); !st) {
        LogError(st.message());
      }
      if (auto st = device.BeginLocalShadowPass(); !st) {
        LogError(st.message());
        device.GpuPassEnd();
        return;
      }
      for (int i = 0; i < lighting.local_shadow_count; ++i) {
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
      device.GpuPassEnd();
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
        device.GpuPassBegin("Opaque");
        if (auto st = device.SetFrameLighting(lighting); !st) {
          LogError(st.message());
          device.GpuPassEnd();
          return;
        }
        if (auto st = device.DrawLitCubes(opaque); !st) {
          LogError(st.message());
        }
        device.GpuPassEnd();
      });
  if (want_ssao || want_taa) {
    graph_.AddPass("PostSSAO_TAA", {"Color", "Depth"}, {"Color"}, [&] {
      device.GpuPassBegin("Post");
      rhi::PostResolveDesc post;
      post.inv_view_proj = lighting.view_proj.Inverse();
      post.eye = lighting.eye;
      post.enable_ssao = want_ssao;
      post.enable_taa = want_taa;
      if (auto st = device.ResolvePostEffects(post); !st) {
        LogError(st.message());
      }
      device.GpuPassEnd();
    });
  }
  if (!transparent.empty()) {
    graph_.AddPass("Transparent", {"Color", "Depth"}, {"Color"}, [&] {
      device.GpuPassBegin("Transparent");
      if (auto st = device.SetFrameLighting(lighting); !st) {
        LogError(st.message());
        device.GpuPassEnd();
        return;
      }
      if (auto st = device.DrawTransparentLitCubes(transparent); !st) {
        LogError(st.message());
      }
      device.GpuPassEnd();
    });
  }
  if (!quads.empty()) {
    graph_.AddPass("UI2D", {"Color"}, {"Color"}, [&] {
      device.GpuPassBegin("UI");
      if (auto st = device.DrawScreenQuads(quads); !st) {
        LogError(st.message());
      }
      device.GpuPassEnd();
    });
  }

  if (auto st = graph_.Compile(); !st) {
    return st;
  }
  if (auto st = graph_.Execute(); !st) {
    return st;
  }
  last_draw_count_ = static_cast<std::uint32_t>(opaque.size() + transparent.size());
  return Status::Ok();
}

}  // namespace engine::render
