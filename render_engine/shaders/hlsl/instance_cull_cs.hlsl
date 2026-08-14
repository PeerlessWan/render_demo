// GPU cull CS: writes IndirectDrawArgs.instance_count (byte offset 4) via InterlockedAdd.
// Visibility is currently always-true for the first `instance_count` threads (CPU list length).

cbuffer CullCB : register(b0) {
  float4x4 view_proj;
  uint instance_count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

RWByteAddressBuffer g_indirect_args : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= instance_count) {
    return;
  }
  // Keep view_proj live for the compiler / PIX.
  const float keep = view_proj[0][0] * 0.0000001;
  if (keep > 1e9) {
    // unreachable
  }
  // D3D12_DRAW_INDEXED_ARGUMENTS.InstanceCount @ offset 4.
  g_indirect_args.InterlockedAdd(4, 1u);
}
