// Mega-W9 C08: minimal cube meshlet MS + PS for D3D12 mesh-shader PSO / DispatchMesh probe.
// Compile: dxc -T ms_6_5 -E MSMain ; dxc -T ps_6_0 -E PSMain

struct VertexOut {
  float4 position : SV_Position;
  float3 color : COLOR0;
};

static const float3 kCubePos[8] = {
    float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5), float3(-0.5, 0.5, -0.5),
    float3(0.5, 0.5, -0.5),   float3(-0.5, -0.5, 0.5), float3(0.5, -0.5, 0.5),
    float3(-0.5, 0.5, 0.5),   float3(0.5, 0.5, 0.5),
};

static const uint3 kCubeTris[12] = {
    uint3(0, 2, 1), uint3(1, 2, 3), uint3(4, 5, 6), uint3(5, 7, 6),
    uint3(0, 1, 4), uint3(1, 5, 4), uint3(2, 6, 3), uint3(3, 6, 7),
    uint3(0, 4, 2), uint3(2, 4, 6), uint3(1, 3, 5), uint3(3, 7, 5),
};

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void MSMain(uint gtid : SV_GroupThreadID, out indices uint3 tris[12],
            out vertices VertexOut verts[8]) {
  SetMeshOutputCounts(8, 12);
  if (gtid < 8) {
    VertexOut v;
    const float3 p = kCubePos[gtid];
    // Simple NDC placement (no CB) so PSO create / DispatchMesh does not need roots.
    v.position = float4(p.xy * 0.4, p.z * 0.4 + 0.5, 1.0);
    v.color = abs(p) + 0.25;
    verts[gtid] = v;
  }
  if (gtid < 12) {
    tris[gtid] = kCubeTris[gtid];
  }
}

float4 PSMain(VertexOut vin) : SV_Target { return float4(vin.color, 1.0); }
