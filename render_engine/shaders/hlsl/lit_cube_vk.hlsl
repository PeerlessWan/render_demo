// Vulkan lit path (HLSL → SPIR-V): CSM + local lights/shadow + albedo/ORM + IBL.
// Uses D3D-style clip matrices; backend applies negative viewport height for upright FB.

cbuffer FrameCB : register(b0) {
  float4x4 g_view_proj;
  float4x4 g_cascade_vp[4];
  float3 g_sun_dir;
  float g_sun_intensity;
  float3 g_ambient;
  float g_shadow_bias;
  float3 g_sun_color;
  float g_specular_power;
  float3 g_eye;
  float g_enable_shadow;
  float4 g_cascade_splits;
  float3 g_cam_forward;
  float g_cascade_count;
  float g_tiles_per_row;
  float g_enable_ibl;
  float g_ibl_intensity;
  float g_enable_reflection;
  float g_reflection_intensity;
  float g_local_count;
  float g_enable_taa;
  float2 g_pad_before_lights;
  float4 g_local_pos_range[32];
  float4 g_local_color_intensity[32];
  float4 g_local_spot[32];       // xyz=dir, w=cosOuter (-1 = point/omni)
  float4 g_local_spot_inner[8];  // cosInner for lights 0..31
  float4x4 g_local_shadow_vp[12];
  float g_enable_local_shadow;
  float g_local_shadow_bias;
  float g_local_shadow_count;
  float g_local_shadow_tiles;
  float4x4 g_prev_view_proj;
  float g_jitter_x;
  float g_jitter_y;
  float g_pad_j0;
  float g_pad_j1;
  float4 g_local_ies[8];  // C03/W7
  // Mega-W10 C02: packed Forward+ tile×Z light lists (8×4×4, ≤8 / cluster).
  float g_enable_tiled_lights;
  float g_tile_grid_w;
  float g_tile_grid_h;
  float g_max_lights_per_tile;
  float g_z_slices;
  float g_z_near;
  float g_z_far;
  float g_pad_z;
  float4 g_tile_light_count[32];
  float4 g_tile_light_index[256];
  // W20 L0: DDGI-lite probe irradiance atlas (not RTXGI) + half-res soft-shadow mask.
  float g_enable_probe_gi;
  float g_probe_gi_intensity;
  float g_probe_rgb_scale;
  float g_probe_nx;
  float3 g_probe_origin;
  float g_probe_ny;
  float3 g_probe_spacing;
  float g_probe_nz;
  float g_enable_soft_shadow_mask;
  float g_pad_w20_0;
  float g_pad_w20_1;
  float g_pad_w20_2;
};

cbuffer ObjectCB : register(b1) {
  float4x4 g_world;
  float4 g_base_color;
  float g_metallic;
  float g_roughness;
  float g_use_albedo;
  float g_use_orm;
  float g_tex_slot;
  float g_uv_scale;
  float g_use_instances;
  float g_pad;
};

// Lit set packing (Vulkan, -fvk-t/s-shift 2):
//   b0/b1 UBO, t0→2 shadow … t6→8 prefilter, t7→9 LUT, t8→10 local shadow,
//   t9→11 instances SSBO, t10→12 reflection probe (separate from IBL prefilter).
// t9 + -fvk-t-shift 2 → binding 11 (after local shadow at t8→10).
[[vk::binding(11, 0)]] StructuredBuffer<float4x4> g_instances : register(t9);

Texture2D g_shadow_map : register(t0);
SamplerComparisonState g_shadow_samp : register(s0);
TextureCube g_ibl_irradiance : register(t1);
SamplerState g_ibl_samp : register(s1);
Texture2D g_albedo_map : register(t2);
SamplerState g_alb_samp : register(s2);
Texture2D g_orm_map : register(t3);
SamplerState g_orm_samp : register(s3);
Texture2D g_albedo_map2 : register(t4);
SamplerState g_alb2_samp : register(s4);
Texture2D g_orm_map2 : register(t5);
SamplerState g_orm2_samp : register(s5);
TextureCube g_ibl_prefilter : register(t6);
SamplerState g_pref_samp : register(s6);
Texture2D g_brdf_lut : register(t7);
SamplerState g_lut_samp : register(s7);
Texture2D g_local_shadow_map : register(t8);
SamplerComparisonState g_local_shadow_samp : register(s8);
TextureCube g_reflection_probe : register(t10);
SamplerState g_probe_samp : register(s10);
// W20 L0: t11→13 probe atlas, t12→14 soft-shadow mask (-fvk-t-shift 2).
Texture2D g_probe_irradiance_atlas : register(t11);
SamplerState g_probe_atlas_samp : register(s11);
Texture2D g_soft_shadow_mask : register(t12);
SamplerState g_soft_shadow_samp : register(s12);

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_Position;
  float3 world_normal : NORMAL;
  float3 world_pos : TEXCOORD0;
  float2 uv : TEXCOORD1;
  float clip_near : SV_ClipDistance0;
};

VSOutput VSMain(VSInput input, uint iid : SV_InstanceID) {
  VSOutput o;
  float4x4 world = g_world;
  if (g_use_instances > 0.5f) {
    world = g_instances[iid];
  }
  float4 wp = mul(world, float4(input.position, 1.0f));
  o.world_pos = wp.xyz;
  o.world_normal = normalize(mul((float3x3)world, input.normal));
  float4 curr = mul(g_view_proj, wp);
  curr.xy += float2(g_jitter_x, g_jitter_y) * curr.w;
  o.position = curr;
  o.uv = input.uv;
  float vz = dot(o.world_pos - g_eye, normalize(g_cam_forward));
  o.clip_near = 1.0;
  if (g_uv_scale > 2.5 && abs(input.normal.y) > 0.85 && vz > 0.0) {
    o.clip_near = vz - 0.25;
  }
  return o;
}

int SelectCascade(float view_depth) {
  int count = (int)g_cascade_count;
  [unroll] for (int i = 0; i < 4; ++i) {
    if (i >= count) {
      return max(count - 1, 0);
    }
    if (view_depth <= g_cascade_splits[i]) {
      return i;
    }
  }
  return max(count - 1, 0);
}

float2 CascadeAtlasUv(float2 uv, int cascade) {
  float tiles = max(g_tiles_per_row, 1.0);
  float tile = 1.0 / tiles;
  float ix = (float)(cascade % (int)tiles);
  float iy = (float)(cascade / (int)tiles);
  float pad = 3.0 / 2048.0;
  float usable = max(tile - 2.0 * pad, tile * 0.5);
  float2 inset = uv * usable + pad;
  return inset + float2(ix, iy) * tile;
}

float SampleCascadeShadow(float3 world_pos, float3 world_n, int c) {
  float3 L = normalize(-g_sun_dir);
  float ndotl = saturate(dot(world_n, L));
  float3 sample_pos = world_pos + world_n * (0.035 + (1.0 - ndotl) * 0.06);

  float4 lp = mul(g_cascade_vp[c], float4(sample_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  // D3D clip Y-up → texture V-down (shadow atlas uses Y-flipped viewport on Vulkan).
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999 || proj.z < 0.0 ||
      proj.z > 1.0) {
    return -1.0;
  }
  float2 atlas_uv = CascadeAtlasUv(uv, c);
  float slope = (1.0 - ndotl) * 2.5;
  float cmp = proj.z - g_shadow_bias * (1.0 + (float)c * 0.35 + slope);

  float tiles = max(g_tiles_per_row, 1.0);
  float tile = 1.0 / tiles;
  float ix = (float)(c % (int)tiles);
  float iy = (float)(c / (int)tiles);
  float2 tile_min = float2(ix, iy) * tile + 1.5 / 2048.0;
  float2 tile_max = float2(ix + 1.0, iy + 1.0) * tile - 1.5 / 2048.0;

  static const float2 kPoisson[8] = {
      float2(-0.326, -0.406), float2(-0.840, -0.074), float2(-0.696, 0.457), float2(-0.203, 0.621),
      float2(0.962, -0.195),  float2(0.473, -0.480),  float2(0.519, 0.767),  float2(0.185, -0.893)};
  float2 texel = 1.0 / 2048.0;
  float shadow = 0;
  [unroll] for (int i = 0; i < 8; ++i) {
    float2 s_uv = clamp(atlas_uv + kPoisson[i] * texel * 2.5, tile_min, tile_max);
    shadow += g_shadow_map.SampleCmpLevelZero(g_shadow_samp, s_uv, cmp);
  }
  return shadow / 8.0;
}

float ShadowFactor(float3 world_pos, float3 world_n) {
  if (g_enable_shadow < 0.5f) {
    return 1.0f;
  }
  float view_depth = max(dot(world_pos - g_eye, normalize(g_cam_forward)), 0.0);
  int start = SelectCascade(view_depth);
  int count = max((int)g_cascade_count, 1);

  float best = -1.0;
  float next_s = -1.0;
  [unroll] for (int c = 0; c < 4; ++c) {
    if (c >= count) {
      continue;
    }
    float s = SampleCascadeShadow(world_pos, world_n, c);
    if (s < 0.0) {
      continue;
    }
    if (c == start) {
      best = s;
    } else if (c == start + 1) {
      next_s = s;
    } else if (best < 0.0 && c > start) {
      best = s;
    } else if (best < 0.0) {
      best = s;
    }
  }
  if (best >= 0.0 && next_s >= 0.0) {
    float split = g_cascade_splits[min(start, 3)];
    float prev = 0.0;
    if (start == 1) {
      prev = g_cascade_splits[0];
    } else if (start == 2) {
      prev = g_cascade_splits[1];
    } else if (start == 3) {
      prev = g_cascade_splits[2];
    }
    float span = max(split - prev, 1e-3);
    float blend_w = max(span * 0.45, 3.0);
    float t = saturate((split - view_depth) / blend_w);
    best = lerp(next_s, best, t);
  }
  return best >= 0.0 ? best : 0.85;
}

float2 LocalShadowAtlasUv(float2 uv, int tile) {
  float tiles = max(g_local_shadow_tiles, 1.0);
  float tile_size = 1.0 / tiles;
  float ix = (float)(tile % (int)tiles);
  float iy = (float)(tile / (int)tiles);
  float2 inset = uv * (tile_size * 0.998) + 0.001 * tile_size;
  return inset + float2(ix, iy) * tile_size;
}

float LocalShadowFactor(float3 world_pos, int light_index) {
  if (g_enable_local_shadow < 0.5f || light_index < 0 || light_index >= (int)g_local_shadow_count) {
    return 1.0f;
  }
  float3 lpos = g_local_pos_range[light_index].xyz;
  float cos_outer = g_local_spot[light_index].w;
  int tile;
  if (cos_outer > -0.999f) {
    tile = light_index * 6;
  } else {
    float3 dir = world_pos - lpos;
    float3 ad = abs(dir);
    int face = 0;
    if (ad.x >= ad.y && ad.x >= ad.z) {
      face = dir.x >= 0.0 ? 0 : 1;
    } else if (ad.y >= ad.z) {
      face = dir.y >= 0.0 ? 2 : 3;
    } else {
      face = dir.z >= 0.0 ? 4 : 5;
    }
    tile = light_index * 6 + face;
  }
  float4 lp = mul(g_local_shadow_vp[tile], float4(world_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999 || proj.z < 0 || proj.z > 1) {
    return 1.0f;
  }
  float2 atlas_uv = LocalShadowAtlasUv(uv, tile);
  float cmp = proj.z - g_local_shadow_bias;
  float shadow = 0;
  float2 texel = 1.0 / 2048.0;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      shadow += g_local_shadow_map.SampleCmpLevelZero(g_local_shadow_samp,
                                                     atlas_uv + float2(x, y) * texel, cmp);
    }
  }
  return shadow / 9.0;
}

float IesProfileAtten(int light_index, float cos_theta) {
  int profile = (int)g_local_ies[light_index >> 2][light_index & 3];
  if (profile <= 0) {
    return 1.0f;
  }
  float c = saturate(cos_theta);
  float u = 1.0f - c;
  if (profile == 1) {
    return pow(1.0f - u, 4.5f);
  }
  if (profile == 2) {
    return pow(1.0f - u, 1.6f);
  }
  float mid = 1.0f - abs(u - 0.45f) * 2.2f;
  return saturate(mid) * (0.35f + 0.65f * (1.0f - u));
}

float SpotConeAtten(int light_index, float3 light_to_frag) {
  float cos_outer = g_local_spot[light_index].w;
  if (cos_outer <= -0.999f) {
    return 1.0f;
  }
  float3 spot_dir = normalize(g_local_spot[light_index].xyz);
  float cos_theta = dot(normalize(light_to_frag), spot_dir);
  float cos_inner = g_local_spot_inner[light_index >> 2][light_index & 3];
  float cone = smoothstep(cos_outer, cos_inner, cos_theta);
  return cone * IesProfileAtten(light_index, cos_theta);
}

float2 ScreenUvFromWorld(float3 world_pos) {
  float4 clip = mul(g_view_proj, float4(world_pos, 1.0f));
  float2 ndc = clip.xy / max(abs(clip.w), 1e-5);
  return saturate(ndc * 0.5f + 0.5f);
}

int TileIndexFromScreenUv(float2 uv) {
  int gw = max((int)g_tile_grid_w, 1);
  int gh = max((int)g_tile_grid_h, 1);
  float u = min(uv.x, 0.999f);
  float v = min(uv.y, 0.999f);
  int tx = min((int)(u * (float)gw), gw - 1);
  int ty = min((int)(v * (float)gh), gh - 1);
  return ty * gw + tx;
}

int ZSliceFromViewZ(float view_z) {
  int ns = max((int)g_z_slices, 1);
  float denom = max(g_z_far - g_z_near, 1e-5);
  float t = saturate((view_z - g_z_near) / denom);
  t = min(t, 0.999f);
  return min((int)(t * (float)ns), ns - 1);
}

int ClusterIndex(int tile, float view_z) {
  int tile_count = max((int)g_tile_grid_w, 1) * max((int)g_tile_grid_h, 1);
  return ZSliceFromViewZ(view_z) * tile_count + tile;
}

float3 AccumulateLocalLight(int i, float3 world_pos, float3 n, float3 diffuse, float ao) {
  float3 lpos = g_local_pos_range[i].xyz;
  float range = max(g_local_pos_range[i].w, 1e-3);
  float3 to_l = lpos - world_pos;
  float dist = length(to_l);
  float3 ld = to_l / max(dist, 1e-5);
  float atten = saturate(1.0 - dist / range);
  atten *= atten;
  atten *= SpotConeAtten(i, world_pos - lpos);
  float nd = saturate(dot(n, ld));
  float3 lcol = g_local_color_intensity[i].rgb * g_local_color_intensity[i].a;
  float lsh = LocalShadowFactor(world_pos, i);
  return diffuse * nd * lcol * atten * ao * lsh;
}

// W20 L0 DDGI-lite: trilinear sample of CPU→GPU irradiance atlas (not RTXGI).
float3 FetchProbeAtlasTexel(int x, int y, int z, int ny, float2 texel, float scale) {
  float u = ((float)x + 0.5) * texel.x;
  float v = ((float)(y + z * ny) + 0.5) * texel.y;
  return g_probe_irradiance_atlas.SampleLevel(g_probe_atlas_samp, float2(u, v), 0).rgb * scale;
}

float3 SampleProbeIrradiance(float3 world_pos) {
  if (g_enable_probe_gi < 0.5f) {
    return 0.xxx;
  }
  int nx = max((int)g_probe_nx, 1);
  int ny = max((int)g_probe_ny, 1);
  int nz = max((int)g_probe_nz, 1);
  float3 spacing = max(g_probe_spacing, 1e-4.xxx);
  float3 f = (world_pos - g_probe_origin) / spacing;
  int x0 = clamp((int)floor(f.x), 0, nx - 1);
  int y0 = clamp((int)floor(f.y), 0, ny - 1);
  int z0 = clamp((int)floor(f.z), 0, nz - 1);
  int x1 = min(x0 + 1, nx - 1);
  int y1 = min(y0 + 1, ny - 1);
  int z1 = min(z0 + 1, nz - 1);
  float tx = saturate(f.x - (float)x0);
  float ty = saturate(f.y - (float)y0);
  float tz = saturate(f.z - (float)z0);
  float2 texel = float2(1.0 / (float)nx, 1.0 / (float)(ny * nz));
  float scale = max(g_probe_rgb_scale, 1e-3);
  float3 c00 = lerp(FetchProbeAtlasTexel(x0, y0, z0, ny, texel, scale),
                    FetchProbeAtlasTexel(x1, y0, z0, ny, texel, scale), tx);
  float3 c10 = lerp(FetchProbeAtlasTexel(x0, y1, z0, ny, texel, scale),
                    FetchProbeAtlasTexel(x1, y1, z0, ny, texel, scale), tx);
  float3 c01 = lerp(FetchProbeAtlasTexel(x0, y0, z1, ny, texel, scale),
                    FetchProbeAtlasTexel(x1, y0, z1, ny, texel, scale), tx);
  float3 c11 = lerp(FetchProbeAtlasTexel(x0, y1, z1, ny, texel, scale),
                    FetchProbeAtlasTexel(x1, y1, z1, ny, texel, scale), tx);
  return lerp(lerp(c00, c10, ty), lerp(c01, c11, ty), tz);
}

float SoftShadowMaskFactor(float3 world_pos) {
  if (g_enable_soft_shadow_mask < 0.5f) {
    return 1.0f;
  }
  float2 uv = ScreenUvFromWorld(world_pos);
  return saturate(g_soft_shadow_mask.SampleLevel(g_soft_shadow_samp, uv, 0).r);
}

float4 PSMain(VSOutput input) : SV_Target {
  float3 n = normalize(input.world_normal);
  // Match D3D lit_cube.hlsl: drop near-camera floor fragments that become a floating slab.
  float vz = dot(input.world_pos - g_eye, normalize(g_cam_forward));
  if (n.y > 0.55) {
    if (vz < 0.25 || input.position.z < 1e-3) {
      discard;
    }
  }
  float3 l = normalize(-g_sun_dir);
  float3 v = normalize(g_eye - input.world_pos);
  float3 h = normalize(l + v);
  float ndotl = saturate(dot(n, l));
  float ndotv = saturate(dot(n, v));

  float2 uv = input.uv * max(g_uv_scale, 1.0);
  float3 base = g_base_color.rgb;
  if (g_use_albedo > 0.5) {
    // W16 ADR 0040: bindless_hot_path sets g_pad to albedo register (2 or 4).
    // Default g_pad=-1 keeps classic tex_slot selection (golden/C4 stable).
    if (g_pad >= 0.0) {
      if (g_pad > 3.0) {
        base *= g_albedo_map2.Sample(g_alb2_samp, uv).rgb;
      } else {
        base *= g_albedo_map.Sample(g_alb_samp, uv).rgb;
      }
    } else if (g_tex_slot > 0.5) {
      base *= g_albedo_map2.Sample(g_alb2_samp, uv).rgb;
    } else {
      base *= g_albedo_map.Sample(g_alb_samp, uv).rgb;
    }
  }

  float metallic = g_metallic;
  float roughness = g_roughness;
  float tex_ao = 1.0;
  if (g_use_orm > 0.5) {
    float3 orm = (g_tex_slot > 0.5) ? g_orm_map2.Sample(g_orm2_samp, uv).rgb
                                    : g_orm_map.Sample(g_orm_samp, uv).rgb;
    tex_ao = orm.r;
    roughness = orm.g;
    metallic = orm.b;
  }

  float spec = pow(saturate(dot(n, h)), max(1.0, g_specular_power * (1.0 - roughness))) *
               (1.0 - roughness) * lerp(0.04, 1.0, metallic) * ndotl;
  // Match D3D lit_cube: fade floor horizon specular harder.
  spec *= ndotv * ndotv * ndotv;
  float sh = ShadowFactor(input.world_pos, n);
  sh *= SoftShadowMaskFactor(input.world_pos);
  float3 diffuse = base * (1.0 - metallic);
  float3 sun_term = (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity * sh * tex_ao;
  sun_term = min(sun_term, 8.0.xxx);
  // Additive IBL (same as D3D lit_cube.hlsl). Replacing ambient with irradiance
  // washed out CSM contact shadows and flattened the whole frame vs D3D12.
  float3 lit = g_ambient * base * tex_ao + sun_term;
  lit += SampleProbeIrradiance(input.world_pos) * base * tex_ao * g_probe_gi_intensity;

  // Fresnel / local reflection probe (dedicated cube; independent of IBL prefilter).
  if (g_enable_reflection > 0.5) {
    float3 R = reflect(-v, n);
    float lod = saturate(roughness) * 4.0;
    float3 env = g_reflection_probe.SampleLevel(g_probe_samp, R, lod).rgb;
    float fres = lerp(0.04, 1.0, metallic);
    fres *= pow(1.0 - ndotv, 5.0) * (1.0 - metallic) + metallic;
    lit += env * fres * g_reflection_intensity * tex_ao;
  }

  if (g_enable_ibl > 0.5) {
    float3 irr = g_ibl_irradiance.Sample(g_ibl_samp, n).rgb;
    lit += diffuse * irr * g_ibl_intensity * tex_ao;
    float3 R = reflect(-v, n);
    float lod = saturate(roughness) * 4.0;
    float3 prefiltered = g_ibl_prefilter.SampleLevel(g_pref_samp, R, lod).rgb;
    float2 brdf = g_brdf_lut.Sample(g_lut_samp, float2(ndotv, saturate(roughness))).rg;
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metallic);
    lit += prefiltered * (f0 * brdf.x + brdf.y) * g_ibl_intensity * tex_ao;
  }

  int lc = (int)g_local_count;
  if (g_enable_tiled_lights > 0.5f && lc > 0) {
    float vz = dot(input.world_pos - g_eye, normalize(g_cam_forward));
    int tile = TileIndexFromScreenUv(ScreenUvFromWorld(input.world_pos));
    int cluster = ClusterIndex(tile, vz);
    int count = (int)g_tile_light_count[cluster >> 2][cluster & 3];
    count = min(count, max((int)g_max_lights_per_tile, 0));
    count = min(count, 8);
    [unroll] for (int s = 0; s < 8; ++s) {
      if (s >= count) {
        break;
      }
      int flat = cluster * 8 + s;
      int i = (int)g_tile_light_index[flat >> 2][flat & 3];
      if (i < 0 || i >= lc) {
        continue;
      }
      lit += AccumulateLocalLight(i, input.world_pos, n, diffuse, tex_ao);
    }
  } else {
    [unroll] for (int i = 0; i < 32; ++i) {
      if (i >= lc) {
        break;
      }
      lit += AccumulateLocalLight(i, input.world_pos, n, diffuse, tex_ao);
    }
  }

  const float alpha = saturate(g_base_color.a);
  if (alpha < 0.999) {
    lit = min(lit, 1.35.xxx);
    lit *= lerp(0.55, 1.0, alpha);
  }
  if (g_enable_taa > 0.5) {
    // Soft hint only; real history TAA is ResolvePostEffects (D3D).
    lit = lerp(lit, lit * 0.98 + g_ambient * base * 0.02, 0.15);
  }
  return float4(lit, alpha);
}

cbuffer ShadowFrameCB : register(b0) {
  float4x4 g_shadow_view_proj;
};

cbuffer ShadowObjectCB : register(b1) {
  float4x4 g_shadow_world;
};

float4 ShadowVS(float3 position : POSITION) : SV_Position {
  float4 wp = mul(g_shadow_world, float4(position, 1.0f));
  return mul(g_shadow_view_proj, wp);
}
