// World-space debug lines (grid / axes / gizmos)

cbuffer DebugFrameCB : register(b0) {
  float4x4 g_view_proj;
};

struct VSIn {
  float3 position : POSITION;
  float4 color : COLOR;
};

struct VSOut {
  float4 position : SV_Position;
  float4 color : COLOR;
};

VSOut VSMain(VSIn input) {
  VSOut o;
  o.position = mul(g_view_proj, float4(input.position, 1.0));
  o.color = input.color;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  return input.color;
}
