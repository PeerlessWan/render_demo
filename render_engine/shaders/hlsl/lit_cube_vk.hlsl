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
  float4 g_local_pos_range[4];
  float4 g_local_color_intensity[4];
  float4x4 g_local_shadow_vp[12];
  float g_enable_local_shadow;
  float g_local_shadow_bias;
  float g_local_shadow_count;
  float g_local_shadow_tiles;
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
};

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

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(g_world, float4(input.position, 1.0f));
  o.world_pos = wp.xyz;
  o.world_normal = normalize(mul((float3x3)g_world, input.normal));
  o.position = mul(g_view_proj, wp);
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
  float2 inset = uv * (tile * 0.998) + 0.001 * tile;
  return inset + float2(ix, iy) * tile;
}

float SampleCascadeShadow(float3 world_pos, int c) {
  float4 lp = mul(g_cascade_vp[c], float4(world_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  // D3D clip Y-up → texture V-down (shadow atlas uses Y-flipped viewport on Vulkan).
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999 || proj.z < 0.0 ||
      proj.z > 1.0) {
    return -1.0;
  }
  float2 atlas_uv = CascadeAtlasUv(uv, c);
  float cmp = proj.z - g_shadow_bias * (1.0 + (float)c * 0.35);
  float shadow = 0;
  float2 texel = 1.0 / 2048.0;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      shadow += g_shadow_map.SampleCmpLevelZero(g_shadow_samp, atlas_uv + float2(x, y) * texel, cmp);
    }
  }
  return shadow / 9.0;
}

float ShadowFactor(float3 world_pos) {
  if (g_enable_shadow < 0.5f) {
    return 1.0f;
  }
  float view_depth = max(dot(world_pos - g_eye, normalize(g_cam_forward)), 0.0);
  int start = SelectCascade(view_depth);
  int count = max((int)g_cascade_count, 1);

  // Prefer the depth-selected cascade, then any other that covers this texel.
  float best = -1.0;
  float next_s = -1.0;
  [unroll] for (int c = 0; c < 4; ++c) {
    if (c >= count) {
      continue;
    }
    float s = SampleCascadeShadow(world_pos, c);
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
    float t = saturate((split - view_depth) / max(span * 0.25, 0.75));
    best = lerp(next_s, best, t);
  }
  // Keep sun contribution stable when outside all cascades (no tint flip to local-only).
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
  int tile = light_index * 6 + face;
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
    if (g_tex_slot > 0.5) {
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
  float sh = ShadowFactor(input.world_pos);
  float3 diffuse = base * (1.0 - metallic);
  float3 sun_term = (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity * sh * tex_ao;
  sun_term = min(sun_term, 8.0.xxx);
  // Additive IBL (same as D3D lit_cube.hlsl). Replacing ambient with irradiance
  // washed out CSM contact shadows and flattened the whole frame vs D3D12.
  float3 lit = g_ambient * base * tex_ao + sun_term;

  // Fresnel reflection probe (same map as IBL prefilter after pack load).
  if (g_enable_reflection > 0.5) {
    float3 R = reflect(-v, n);
    float lod = saturate(roughness) * 4.0;
    float3 env = g_ibl_prefilter.SampleLevel(g_pref_samp, R, lod).rgb;
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
  [unroll] for (int i = 0; i < 4; ++i) {
    if (i >= lc) {
      break;
    }
    float3 lpos = g_local_pos_range[i].xyz;
    float range = max(g_local_pos_range[i].w, 1e-3);
    float3 to_l = lpos - input.world_pos;
    float dist = length(to_l);
    float3 ld = to_l / max(dist, 1e-5);
    float atten = saturate(1.0 - dist / range);
    atten *= atten;
    float nd = saturate(dot(n, ld));
    float3 lcol = g_local_color_intensity[i].rgb * g_local_color_intensity[i].a;
    float lsh = LocalShadowFactor(input.world_pos, i);
    lit += diffuse * nd * lcol * atten * tex_ao * lsh;
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
