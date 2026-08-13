// Screen-space post: depth SSAO + history TAA resolve
cbuffer PostCB : register(b0) {
  float2 g_inv_res;
  float g_enable_ssao;
  float g_enable_taa;
  float g_ssao_radius;
  float g_ssao_intensity;
  float g_taa_blend;
  float g_exposure;
  float4x4 g_inv_view_proj;
  float3 g_eye;
  float g_pad2;
};

Texture2D g_scene_color : register(t0);
Texture2D g_scene_depth : register(t1);
Texture2D g_history : register(t2);
SamplerState g_linear : register(s0);
SamplerState g_point : register(s1);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  float2 uv = float2((id << 1) & 2, id & 2);
  o.uv = uv;
  o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 ReconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
  clip.y *= -1.0;
  float4 world = mul(g_inv_view_proj, clip);
  return world.xyz / max(world.w, 1e-5);
}

float SSAO(float2 uv, float3 origin, float3 normal) {
  float occl = 0.0;
  // 8-tap spiral in screen space scaled by radius
  [unroll] for (int i = 0; i < 8; ++i) {
    float a = (float)i * 2.399963;
    float r = g_ssao_radius * (0.25 + 0.75 * (float)i / 7.0);
    float2 offset = float2(cos(a), sin(a)) * r * g_inv_res;
    float2 suv = saturate(uv + offset);
    float sd = g_scene_depth.Sample(g_point, suv).r;
    if (sd >= 0.9999) {
      continue;
    }
    float3 sample_pos = ReconstructWorld(suv, sd);
    float3 v = sample_pos - origin;
    float dist = length(v);
    float3 dir = v / max(dist, 1e-5);
    float nd = saturate(dot(normal, dir));
    float atten = 1.0 - saturate(dist / (g_ssao_radius * 25.0));
    occl += nd * atten;
  }
  float ao = 1.0 - saturate(occl / 8.0) * g_ssao_intensity;
  return ao;
}

float4 PSMain(VSOut input) : SV_Target {
  float2 uv = input.uv;
  float3 color = g_scene_color.Sample(g_linear, uv).rgb;
  float depth = g_scene_depth.Sample(g_point, uv).r;

  if (g_enable_ssao > 0.5 && depth < 0.9999) {
    float3 origin = ReconstructWorld(uv, depth);
    // Normal from depth neighbors
    float2 texel = g_inv_res;
    float3 p1 = ReconstructWorld(uv + float2(texel.x, 0), g_scene_depth.Sample(g_point, uv + float2(texel.x, 0)).r);
    float3 p2 = ReconstructWorld(uv + float2(0, texel.y), g_scene_depth.Sample(g_point, uv + float2(0, texel.y)).r);
    float3 n = normalize(cross(p1 - origin, p2 - origin));
    float ao = SSAO(uv, origin, n);
    color *= ao;
  }

  if (g_enable_taa > 0.5) {
    float3 hist = g_history.Sample(g_linear, uv).rgb;
    // Neighborhood clamp (3x3)
    float3 cmin = color;
    float3 cmax = color;
    [unroll] for (int y = -1; y <= 1; ++y) {
      [unroll] for (int x = -1; x <= 1; ++x) {
        float3 n = g_scene_color.Sample(g_point, uv + float2(x, y) * g_inv_res).rgb;
        cmin = min(cmin, n);
        cmax = max(cmax, n);
      }
    }
    hist = clamp(hist, cmin, cmax);
    color = lerp(color, hist, g_taa_blend);
  }

  color *= max(g_exposure, 0.0);
  // Reinhard tone map keeps bright areas from clipping after exposure.
  color = color / (1.0 + color);
  return float4(color, 1.0);
}
