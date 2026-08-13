// Immediate-mode UI mesh (ImGui font atlas + textured triangles)

cbuffer UiCB : register(b0) {
  float2 g_inv_display_size;
  float2 g_pad;
};

Texture2D g_font : register(t0);
SamplerState g_samp : register(s0);

struct VSIn {
  float2 pos : POSITION;
  float2 uv : TEXCOORD0;
  float4 col : COLOR;
};

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float4 col : COLOR;
};

VSOut VSMain(VSIn input) {
  VSOut o;
  float2 ndc;
  ndc.x = input.pos.x * g_inv_display_size.x * 2.0 - 1.0;
  ndc.y = 1.0 - input.pos.y * g_inv_display_size.y * 2.0;
  o.pos = float4(ndc, 0.0, 1.0);
  o.uv = input.uv;
  o.col = input.col;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  return input.col * g_font.Sample(g_samp, input.uv);
}
