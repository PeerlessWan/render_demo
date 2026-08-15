// Vulkan GPU cull CS (SPIR-V via DXC): always-visible for first `instance_count`
// threads — matches D3D instance_cull_cs.hlsl. Writes IndirectArgs.instanceCount
// (uint index 1) via InterlockedAdd + compact indices.

struct CullPC {
  float4x4 view_proj;
  uint instance_count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

[[vk::push_constant]] CullPC pc;

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> g_indirect_args;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> g_compact_indices;

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= pc.instance_count) {
    return;
  }
  const float keep = pc.view_proj[0][0] * 0.0000001;
  if (keep > 1e9) {
    // unreachable — keep view_proj live for compiler
  }
  // VkDrawIndexedIndirectCommand.instanceCount @ uint index 1.
  uint slot = 0;
  InterlockedAdd(g_indirect_args[1], 1u, slot);
  g_compact_indices[slot] = dtid.x;
}
