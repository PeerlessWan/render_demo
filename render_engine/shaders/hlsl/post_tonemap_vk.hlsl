// Vulkan post: sample scene_color then apply exposure/tonemap to swapchain.

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

struct PostPC {
  float exposure;
  float tonemap_mode;  // 0=none 1=reinhard-ish 2=ACES-ish
  float enable_ssao;
  float enable_taa;
};

[[vk::push_constant]] PostPC pc;

Texture2D g_scene_color : register(t0);
SamplerState g_linear_samp : register(s1);

VSOut VSMain(uint vid : SV_VertexID) {
  VSOut o;
  const float2 uv = float2((vid << 1) & 2, vid & 2);
  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  o.uv = uv;
  return o;
}

float3 Tonemap(float3 c, float e, float mode) {
  float3 x = max(c * max(e, 0.01), 0.0.xxx);
  if (mode >= 1.5) {
    // ACES fitted
    x = saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
  } else if (mode >= 0.5) {
    x = x / (1.0 + x);
  }
  if (pc.enable_ssao > 0.5) {
    x *= 0.98;
  }
  if (pc.enable_taa > 0.5) {
    x *= 0.99;
  }
  return x;
}

float4 PSMain(VSOut i) : SV_Target {
  float3 scene = g_scene_color.Sample(g_linear_samp, i.uv).rgb;
  float3 outc = Tonemap(scene, pc.exposure, pc.tonemap_mode);
  return float4(outc, 1.0);
}
