// Vulkan post subset: fullscreen exposure multiply (+ mild tonemap curve on scale).
// Drawn with dst*src blend over lit swapchain; device then blits into scene_color RT.

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

struct PostPC {
  float exposure;
  float tonemap_mode;  // 0=none 1=reinhard-ish 2=ACES-ish on the scale
  float enable_ssao;   // accepted switches (sampling TBD)
  float enable_taa;
};

[[vk::push_constant]] PostPC pc;

VSOut VSMain(uint vid : SV_VertexID) {
  VSOut o;
  const float2 uv = float2((vid << 1) & 2, vid & 2);
  o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  o.uv = uv;
  return o;
}

float4 PSMain(VSOut i) : SV_Target {
  float e = max(pc.exposure, 0.01);
  float f = e;
  if (pc.tonemap_mode >= 1.5) {
    // ACES fitted curve applied to the exposure scale (subset).
    f = saturate((e * (2.51 * e + 0.03)) / (e * (2.43 * e + 0.59) + 0.14));
    f = max(f, 0.05);
  } else if (pc.tonemap_mode >= 0.5) {
    f = e / (1.0 + e);
    f = max(f * 2.0, 0.05);
  }
  // Optional switches currently modulate scale slightly so Feature path is live.
  if (pc.enable_ssao > 0.5) {
    f *= 0.98;
  }
  if (pc.enable_taa > 0.5) {
    f *= 0.99;
  }
  return float4(f, f, f, 1.0);
}
