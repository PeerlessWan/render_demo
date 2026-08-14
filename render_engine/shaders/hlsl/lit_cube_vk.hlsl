// Vulkan lit cube with directional CSM + optional local-shadow atlas sample (HLSL → SPIR-V).

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
  float g_enable_local_shadow;
  float g_local_shadow_bias;
  float g_local_shadow_tiles;
  float _pad_local;
  float4x4 g_local_shadow_vp;
};

cbuffer ObjectCB : register(b1) {
  float4x4 g_world;
  float4 g_base_color;
  float g_metallic;
  float g_roughness;
  float2 _pad_obj;
};

Texture2D g_shadow_map : register(t0);
SamplerComparisonState g_shadow_samp : register(s0);
TextureCube g_ibl_irradiance : register(t1);
SamplerState g_ibl_samp : register(s1);

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float2 uv : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_Position;
  float3 world_normal : NORMAL;
  float3 world_pos : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  float4 wp = mul(g_world, float4(input.position, 1.0f));
  o.world_pos = wp.xyz;
  o.world_normal = normalize(mul((float3x3)g_world, input.normal));
  o.position = mul(g_view_proj, wp);
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

// Local-shadow deepen: sample shared atlas with local light VP (tile 0 layout).
float LocalShadowFactor(float3 world_pos) {
  if (g_enable_local_shadow < 0.5f) {
    return 1.0f;
  }
  float4 lp = mul(g_local_shadow_vp, float4(world_pos, 1.0f));
  float3 proj = lp.xyz / max(lp.w, 1e-5);
  float2 uv = proj.xy * float2(0.5, -0.5) + 0.5;
  if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1 || proj.z < 0 || proj.z > 1) {
    return 1.0f;
  }
  float tiles = max(g_local_shadow_tiles, 1.0);
  float tile = 1.0 / tiles;
  // Tile 0 occupies top-left of the shared atlas (matches BindLocalShadowTile(0)).
  float2 atlas_uv = uv * (tile * 0.998) + 0.001 * tile;
  float cmp = proj.z - g_local_shadow_bias;
  float shadow = 0;
  float2 texel = 1.0 / 2048.0;
  [unroll] for (int y = -1; y <= 1; ++y) {
    [unroll] for (int x = -1; x <= 1; ++x) {
      shadow += g_shadow_map.SampleCmpLevelZero(g_shadow_samp, atlas_uv + float2(x, y) * texel, cmp);
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

  float3 base = g_base_color.rgb;
  float metallic = g_metallic;
  float roughness = g_roughness;
  float spec = pow(saturate(dot(n, h)), max(1.0, g_specular_power * (1.0 - roughness))) *
               (1.0 - roughness) * lerp(0.04, 1.0, metallic);
  float sh = ShadowFactor(input.world_pos);
  float lsh = LocalShadowFactor(input.world_pos);
  sh = min(sh, lsh);
  float3 diffuse = base * (1.0 - metallic);
  float3 ambient = g_ambient * base;
  if (g_enable_ibl > 0.5) {
    float3 irr = g_ibl_irradiance.Sample(g_ibl_samp, n).rgb;
    ambient = lerp(ambient, irr * base * g_ibl_intensity, 0.85);
  }
  float3 lit = ambient + (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity * sh;
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
