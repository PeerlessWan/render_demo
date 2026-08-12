// M2 triangle + textured sample (HLSL → DXIL via tools/shader_compile)

struct VSInput {
  float3 position : POSITION;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
};

struct VSOutput {
  float4 position : SV_Position;
  float3 color : COLOR;
  float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  o.position = float4(input.position, 1.0f);
  o.color = input.color;
  o.uv = input.uv;
  return o;
}

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(VSOutput input) : SV_Target {
  float4 tex = g_texture.Sample(g_sampler, input.uv);
  return float4(input.color * tex.rgb, 1.0f);
}
