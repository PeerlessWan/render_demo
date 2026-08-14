// Skybox: unit cube, far-plane depth, sample environment cubemap.

cbuffer SkyCB : register(b0) {
  float4x4 g_view_rot_proj;
};

TextureCube g_sky : register(t0);
SamplerState g_samp : register(s0);

struct VSOut {
  float4 position : SV_Position;
  float3 dir : TEXCOORD0;
};

static const float3 kCube[36] = {
  // -Z
  {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,-1,-1},{1,1,-1},{-1,1,-1},
  // +Z
  {1,-1,1},{-1,-1,1},{-1,1,1},{1,-1,1},{-1,1,1},{1,1,1},
  // -X
  {-1,-1,1},{-1,-1,-1},{-1,1,-1},{-1,-1,1},{-1,1,-1},{-1,1,1},
  // +X
  {1,-1,-1},{1,-1,1},{1,1,1},{1,-1,-1},{1,1,1},{1,1,-1},
  // -Y
  {-1,-1,1},{1,-1,1},{1,-1,-1},{-1,-1,1},{1,-1,-1},{-1,-1,-1},
  // +Y
  {-1,1,-1},{1,1,-1},{1,1,1},{-1,1,-1},{1,1,1},{-1,1,1},
};

VSOut VSMain(uint vid : SV_VertexID) {
  VSOut o;
  float3 p = kCube[vid];
  o.dir = p;
  float4 clip = mul(g_view_rot_proj, float4(p, 1.0));
  // Force far plane so sky loses to real geometry.
  clip.z = clip.w;
  o.position = clip;
  return o;
}

float4 PSMain(VSOut input) : SV_Target {
  float3 c = g_sky.SampleLevel(g_samp, normalize(input.dir), 0).rgb;
  // Mild boost so LDR sky reads well under HDR tonemap.
  return float4(c * 1.35, 1.0);
}
