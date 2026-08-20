// W17 ADR 0041: GPU particle integrate CS (Vulkan / SPIR-V via DXC).
// Explicit vk bindings — matches ephemeral SSBO + push-constant path in gpu_particles.cpp.

struct ParticlePC {
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

[[vk::push_constant]] ParticlePC pc;
[[vk::binding(0, 0)]] RWStructuredBuffer<ParticleGpu> g_particles;

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
  const uint i = id.x;
  if (i >= (uint)pc.g_count) {
    return;
  }
  ParticleGpu p = g_particles[i];
  p.life -= pc.g_dt;
  p.position += p.velocity * pc.g_dt;
  p.velocity.y -= 2.5 * pc.g_dt;
  g_particles[i] = p;
}
