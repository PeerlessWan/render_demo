// Usable forward lit cube (HLSL → DXIL)

cbuffer FrameCB : register(b0) {
  float4x4 g_view_proj;
  float3 g_sun_dir;
  float g_sun_intensity;
  float3 g_ambient;
  float g_pad0;
  float3 g_sun_color;
  float g_pad1;
};

cbuffer ObjectCB : register(b1) {
  float4x4 g_world;
  float4 g_base_color;
};

struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
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

float4 PSMain(VSOutput input) : SV_Target {
  float3 n = normalize(input.world_normal);
  float3 l = normalize(-g_sun_dir);
  float ndotl = saturate(dot(n, l));
  float3 lit = g_ambient + g_sun_color * g_sun_intensity * ndotl;
  return float4(g_base_color.rgb * lit, g_base_color.a);
}
