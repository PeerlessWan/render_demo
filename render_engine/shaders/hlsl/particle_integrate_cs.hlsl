// W16 ADR 0040: GPU particle integrate CS (D3D12). Structured buffer of ParticleGpu.
// Layout must match engine::vfx::ParticleGpu in gpu_particles.cpp.

cbuffer ParticleCB : register(b0) {
  float g_dt;
  float g_count;
  float g_pad0;
  float g_pad1;
};

struct ParticleGpu {
  float3 position;
  float life;
  float3 velocity;
  float size;
  float4 color;
};

RWStructuredBuffer<ParticleGpu> g_particles : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  const uint i = id.x;
  if (i >= (uint)g_count) {
    return;
  }
  ParticleGpu p = g_particles[i];
  p.life -= g_dt;
  p.position += p.velocity * g_dt;
  p.velocity.y -= 2.5 * g_dt;
  g_particles[i] = p;
}
