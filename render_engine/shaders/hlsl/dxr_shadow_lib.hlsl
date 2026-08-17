// Minimal DXR library (W7): raygen / miss / closesthit writing a tiny UAV buffer.
// Compile: dxc -T lib_6_3 -Fo dxr_shadow_lib.cso dxr_shadow_lib.hlsl

RaytracingAccelerationStructure g_scene : register(t0);
RWStructuredBuffer<float4> g_output : register(u0);

struct Payload {
  float3 color;
};

[shader("raygeneration")]
void RayGen() {
  const uint2 idx = DispatchRaysIndex().xy;
  // Fixed 8x8 dispatch contract (matches TryBuildCubeBlasTlasAndDispatchRays).
  const uint out_i = idx.y * 8u + idx.x;
  RayDesc ray;
  ray.Origin = float3(0.0, 0.0, -2.0);
  ray.Direction = float3(0.0, 0.0, 1.0);
  ray.TMin = 0.001;
  ray.TMax = 100.0;
  Payload p;
  p.color = float3(0.0, 0.0, 0.0);
  TraceRay(g_scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
  g_output[out_i] = float4(p.color, 1.0);
}

[shader("miss")]
void Miss(inout Payload p) {
  p.color = float3(0.15, 0.15, 0.25);
}

[shader("closesthit")]
void ClosestHit(inout Payload p, in BuiltInTriangleIntersectionAttributes attr) {
  p.color = float3(0.85, 0.25, 0.15) * (1.0 - attr.barycentrics.x * 0.3);
}
