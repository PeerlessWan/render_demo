// Minimal directional lit cube for Vulkan (HLSL → SPIR-V via DXC).
// No shadows / textures — matches IDevice SetupLitMesh + DrawLitCube smoke path.

cbuffer FrameCB : register(b0) {
  float4x4 g_view_proj;
  float3 g_sun_dir;
  float g_sun_intensity;
  float3 g_ambient;
  float g_specular_power;
  float3 g_sun_color;
  float _pad_sun;
  float3 g_eye;
  float _pad_eye;
};

cbuffer ObjectCB : register(b1) {
  float4x4 g_world;
  float4 g_base_color;
  float g_metallic;
  float g_roughness;
  float2 _pad_obj;
};

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
  float3 diffuse = base * (1.0 - metallic);
  float3 lit = g_ambient * base + (diffuse * ndotl + g_sun_color * spec) * g_sun_intensity;
  return float4(lit, g_base_color.a);
}
