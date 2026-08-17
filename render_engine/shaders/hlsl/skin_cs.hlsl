// GPU skin CS (W7/C12): bind positions + bone matrices + per-vertex indices/weights
// → skinned positions. Contract matches SkinVerticesGpuDispatchStub / SkinVertexCpu.
// Matrices are column-major float4x4 (matches engine::Mat4).

cbuffer SkinCB : register(b0) {
  uint vertex_count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

StructuredBuffer<float3> g_bind_pos : register(t0);
StructuredBuffer<float4x4> g_bones : register(t1);
StructuredBuffer<int4> g_bone_indices : register(t2);
StructuredBuffer<float4> g_bone_weights : register(t3);
RWStructuredBuffer<float3> g_out_pos : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= vertex_count) {
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
