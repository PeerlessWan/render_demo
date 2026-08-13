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
  float4x4 g_local_shadow_vp[4];
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
};

Texture2D g_shadow_map : register(t0);
Texture2D g_albedo_map : register(t1);
Texture2D g_local_shadow_map : register(t2);
Texture2D g_orm_map : register(t3);
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
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(g_world, float4(input.position, 1.0f));
  o.world_pos = wp.xyz;
  o.world_normal = normalize(mul((float3x3)g_world, input.normal));
  o.position = mul(g_view_proj, wp);
  o.uv = input.uv;
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

float ShadowFactor(float3 world_pos) {
  if (g_enable_shadow < 0.5f) {
    return 1.0f;
  }
  float view_depth = dot(world_pos - g_eye, normalize(g_cam_forward));
  int cascade = SelectCascade(view_depth);
  float4 lp = mul(g_cascade_vp[cascade], float4(world_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1 || proj.z < 0 || proj.z > 1) {
    return 1.0f;
  }
  float2 atlas_uv = CascadeAtlasUv(uv, cascade);
  float cmp = proj.z - g_shadow_bias * (1.0 + (float)cascade * 0.35);
  float shadow = 0;
  float2 texel = 1.0 / 2048.0;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      shadow += g_shadow_map.SampleCmpLevelZero(g_shadow_samp, atlas_uv + float2(x, y) * texel, cmp);
    }
  }
  return shadow / 9.0;
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
  float4 lp = mul(g_local_shadow_vp[light_index], float4(world_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0.001 || uv.x > 0.999 || uv.y < 0.001 || uv.y > 0.999 || proj.z < 0 || proj.z > 1) {
    return 1.0f;
  }
  float2 atlas_uv = LocalShadowAtlasUv(uv, light_index);
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
  float3 l = normalize(-g_sun_dir);
  float3 v = normalize(g_eye - input.world_pos);
  float3 h = normalize(l + v);
  float ndotl = saturate(dot(n, l));

  float2 uv = input.uv * 2.0;
  float3 base = g_base_color.rgb;
  if (g_use_albedo > 0.5) {
    base *= g_albedo_map.Sample(g_linear_samp, uv).rgb;
  }

  float metallic = g_metallic;
  float roughness = g_roughness;
  float tex_ao = 1.0;
  if (g_use_orm > 0.5) {
    float3 orm = g_orm_map.Sample(g_linear_samp, uv).rgb;
    tex_ao = orm.r;
    roughness = orm.g;
    metallic = orm.b;
  }

  float spec = pow(saturate(dot(n, h)), max(1.0, g_specular_power * (1.0 - roughness))) *
               (1.0 - roughness) * lerp(0.04, 1.0, metallic);
  float sh = ShadowFactor(input.world_pos);

  float ao = tex_ao;
  // Screen-space AO applied in ResolvePostEffects; keep flag unused here.

  float3 diffuse = base * (1.0 - metallic);
  float3 lit = g_ambient * base * ao + (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity * sh * ao;

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
