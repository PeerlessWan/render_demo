// Vulkan GPU skin CS (Mega-W8 / C12): same contract as skin_cs.hlsl / SkinVertexCpu.
// Explicit bindings for SPIR-V; compile with -fvk-use-dx-layout.

struct SkinCB {
  uint vertex_count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

[[vk::binding(0, 0)]] cbuffer SkinCB_b : register(b0) {
  SkinCB cb;
};

[[vk::binding(1, 0)]] StructuredBuffer<float3> g_bind_pos;
[[vk::binding(2, 0)]] StructuredBuffer<float4x4> g_bones;
[[vk::binding(3, 0)]] StructuredBuffer<int4> g_bone_indices;
[[vk::binding(4, 0)]] StructuredBuffer<float4> g_bone_weights;
[[vk::binding(5, 0)]] RWStructuredBuffer<float3> g_out_pos;

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= cb.vertex_count) {
    return;
  }
  const float3 bind = g_bind_pos[dtid.x];
  const int4 bones = g_bone_indices[dtid.x];
  const float4 weights = g_bone_weights[dtid.x];
  float3 out_p = 0.xxx;
  [unroll]
  for (int i = 0; i < 4; ++i) {
    const float w = weights[i];
    if (w <= 0.0) {
      continue;
    }
    const int b = bones[i];
    if (b < 0) {
      continue;
    }
    const float4 hp = mul(g_bones[b], float4(bind, 1.0));
    out_p += hp.xyz * w;
  }
  g_out_pos[dtid.x] = out_p;
}
