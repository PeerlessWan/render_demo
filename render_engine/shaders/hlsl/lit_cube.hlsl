// Product-leaning forward lit cube + CSM + local spot shadow + albedo

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
  float g_enable_ssao;
  float g_enable_taa;
  float g_local_count;
  float4 g_local_pos_range[4];
  float4 g_local_color_intensity[4];
  float4x4 g_local_shadow_vp[12];
  float g_enable_local_shadow;
  float g_local_shadow_bias;
  float g_local_shadow_count;
  float g_local_shadow_tiles;
  float4x4 g_prev_view_proj;
  float g_jitter_x;
  float g_jitter_y;
  float g_enable_reflection;
  float g_reflection_intensity;
  float g_enable_ibl;
  float g_ibl_intensity;
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
  float2 g_pad;
};

Texture2D g_shadow_map : register(t0);
Texture2D g_albedo_map : register(t1);
Texture2D g_local_shadow_map : register(t2);
Texture2D g_orm_map : register(t3);
Texture2D g_albedo_map2 : register(t4);
Texture2D g_orm_map2 : register(t5);
TextureCube g_reflection_map : register(t6);  // specular prefilter / probe
TextureCube g_ibl_irradiance : register(t7);
Texture2D g_brdf_lut : register(t8);
SamplerComparisonState g_shadow_samp : register(s0);
SamplerState g_linear_samp : register(s1);

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
  float2 velocity : TEXCOORD2;  // NDC motion (curr - prev)
  // Extra clip plane in front of the camera near plane. Stops floor tris from
  // straddling z_near (which otherwise becomes a floating screen-space slab).
  float clip_near : SV_ClipDistance0;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(g_world, float4(input.position, 1.0f));
  o.world_pos = wp.xyz;
  o.world_normal = normalize(mul((float3x3)g_world, input.normal));
  float4 curr = mul(g_view_proj, wp);
  float4 prev = mul(g_prev_view_proj, wp);
  // Sub-pixel jitter in clip space (NDC xy * w).
  curr.xy += float2(g_jitter_x, g_jitter_y) * curr.w;
  o.position = curr;
  float2 curr_ndc = curr.xy / max(curr.w, 1e-5);
  float2 prev_ndc = prev.xy / max(prev.w, 1e-5);
  o.velocity = curr_ndc - prev_ndc;
  o.uv = input.uv;
  float vz = dot(o.world_pos - g_eye, normalize(g_cam_forward));
  // Floors: clip anything closer than 0.35m in view space.
  // Other meshes keep full geometry (negative clip distance never triggers).
  o.clip_near = (abs(input.normal.y) > 0.85) ? (vz - 0.35) : 1.0;
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
      shadow +=
          g_shadow_map.SampleCmpLevelZero(g_shadow_samp, atlas_uv + float2(x, y) * texel, cmp);
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
  // Do NOT fade shadow→0 near atlas edges: that kills sun and leaves only local
  // lights (cool blue) — reads as a floating cyan slab that tracks camera motion.
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
      shadow +=
          g_local_shadow_map.SampleCmpLevelZero(g_shadow_samp, atlas_uv + float2(x, y) * texel, cmp);
    }
  }
  return shadow / 9.0;
}

float4 PSMain(VSOutput input) : SV_Target {
  float3 n = normalize(input.world_normal);
  // Large floor tris that straddle the near plane get clipped into a screen-filling
  // slab with depth≈z_near. That slab floats ABOVE the real grid and cuts props.
  // Drop those fragments (and anything behind the camera on floors).
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

  float2 uv = input.uv * max(g_uv_scale, 1.0);
  float3 base = g_base_color.rgb;
  if (g_use_albedo > 0.5) {
    if (g_tex_slot > 0.5) {
      base *= g_albedo_map2.Sample(g_linear_samp, uv).rgb;
    } else {
      base *= g_albedo_map.Sample(g_linear_samp, uv).rgb;
    }
  }

  float metallic = g_metallic;
  float roughness = g_roughness;
  float tex_ao = 1.0;
  if (g_use_orm > 0.5) {
    float3 orm = (g_tex_slot > 0.5) ? g_orm_map2.Sample(g_linear_samp, uv).rgb
                                    : g_orm_map.Sample(g_linear_samp, uv).rgb;
    tex_ao = orm.r;
    roughness = orm.g;
    metallic = orm.b;
  }

  float spec = pow(saturate(dot(n, h)), max(1.0, g_specular_power * (1.0 - roughness))) *
               (1.0 - roughness) * lerp(0.04, 1.0, metallic) * ndotl;
  // Grazing views of large floors create a bright horizon band that reads as a
  // floating white slab; fade specular when looking across the surface.
  float ndotv = saturate(dot(n, v));
  spec *= ndotv * ndotv;
  float sh = ShadowFactor(input.world_pos);

  float ao = tex_ao;
  // Screen-space AO applied in ResolvePostEffects; keep flag unused here.

  float3 diffuse = base * (1.0 - metallic);
  // Keep sun term bounded; HDR RT + tonemap preserve textured floors.
  float3 sun_term = (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity * sh * ao;
  sun_term = min(sun_term, 8.0.xxx);
  float3 lit = g_ambient * base * ao + sun_term;

  if (g_enable_reflection > 0.5) {
    float3 R = reflect(-v, n);
    float lod = saturate(roughness) * 4.0;
    float3 env = g_reflection_map.SampleLevel(g_linear_samp, R, lod).rgb;
    float fres = lerp(0.04, 1.0, metallic);
    fres *= pow(1.0 - ndotv, 5.0) * (1.0 - metallic) + metallic;
    lit += env * fres * g_reflection_intensity * ao;
  }

  if (g_enable_ibl > 0.5) {
    float3 irradiance = g_ibl_irradiance.Sample(g_linear_samp, n).rgb;
    lit += diffuse * irradiance * g_ibl_intensity * ao;
    float3 R = reflect(-v, n);
    float lod = saturate(roughness) * 4.0;
    float3 prefiltered = g_reflection_map.SampleLevel(g_linear_samp, R, lod).rgb;
    float2 brdf = g_brdf_lut.Sample(g_linear_samp, float2(ndotv, saturate(roughness))).rg;
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), base, metallic);
    lit += prefiltered * (f0 * brdf.x + brdf.y) * g_ibl_intensity * ao;
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
    lit += diffuse * nd * lcol * atten * ao * lsh;
  }

  if (g_enable_taa > 0.5) {
    // Soft hint only; real history TAA is ResolvePostEffects.
    lit = lerp(lit, lit * 0.98 + g_ambient * base * 0.02, 0.15);
  }
  return float4(lit, g_base_color.a);
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

float4 ShadowPS() : SV_Target { return 0; }

struct QuadIn {
  float2 ndc : POSITION;
  float4 color : COLOR;
};
struct QuadOut {
  float4 pos : SV_Position;
  float4 color : COLOR;
};
QuadOut QuadVS(QuadIn input) {
  QuadOut o;
  o.pos = float4(input.ndc, 0.0f, 1.0f);
  o.color = input.color;
  return o;
}
float4 QuadPS(QuadOut input) : SV_Target { return input.color; }
