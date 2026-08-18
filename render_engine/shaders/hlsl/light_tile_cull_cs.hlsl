// Mega-W9 C02: Forward+ light → screen-tile cull CS.
// Layout matches FrameCB: tile_light_count[32], tile_light_index[256]
// (grid 8×4, ≤8 lights/tile, ≤16 lights). Range sphere → UV AABB via center + ±range XYZ.
// CPU reference: engine::render::AssignLightsToTiles / SimulateLightTileCullCs.

static const uint kGridW = 8;
static const uint kGridH = 4;
static const uint kTileCount = 32;
static const uint kMaxLights = 16;
static const uint kMaxPerTile = 8;

struct LightPacked {
  float3 pos;
  float range;
};

cbuffer TileCullCB : register(b0) {
  float4x4 view_proj;
  uint light_count;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

StructuredBuffer<LightPacked> g_lights : register(t0);
RWStructuredBuffer<int> g_tile_light_count : register(u0);   // [32]
RWStructuredBuffer<int> g_tile_light_index : register(u1);   // [256]

bool ProjectWorldToUv(float3 p, out float u, out float v) {
  float4 clip = mul(view_proj, float4(p, 1.0));
  if (clip.w <= 1e-5) {
    u = 0;
    v = 0;
    return false;
  }
  float2 ndc = clip.xy / clip.w;
  u = ndc.x * 0.5 + 0.5;
  v = ndc.y * 0.5 + 0.5;
  return !(isnan(u) || isnan(v) || isinf(u) || isinf(v));
}

bool LightUvAabb(float3 pos, float range, out float u0, out float u1, out float v0, out float v1) {
  float r = max(range, 0.0);
  float3 offsets[7] = {
      float3(0, 0, 0), float3(r, 0, 0), float3(-r, 0, 0), float3(0, r, 0),
      float3(0, -r, 0), float3(0, 0, r), float3(0, 0, -r),
  };
  u0 = 1e9;
  u1 = -1e9;
  v0 = 1e9;
  v1 = -1e9;
  int accepted = 0;
  [unroll]
  for (int i = 0; i < 7; ++i) {
    float u, v;
    if (!ProjectWorldToUv(pos + offsets[i], u, v)) {
      continue;
    }
    u0 = min(u0, u);
    u1 = max(u1, u);
    v0 = min(v0, v);
    v1 = max(v1, v);
    ++accepted;
  }
  return accepted > 0;
}

[numthreads(8, 4, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= kGridW || dtid.y >= kGridH) {
    return;
  }
  const uint tile = dtid.y * kGridW + dtid.x;
  const float tile_u0 = (float)dtid.x / (float)kGridW;
  const float tile_u1 = (float)(dtid.x + 1) / (float)kGridW;
  const float tile_v0 = (float)dtid.y / (float)kGridH;
  const float tile_v1 = (float)(dtid.y + 1) / (float)kGridH;

  int count = 0;
  int slots[8];
  [unroll]
  for (int s = 0; s < 8; ++s) {
    slots[s] = -1;
  }

  const uint n = min(light_count, kMaxLights);
  for (uint li = 0; li < n; ++li) {
    LightPacked L = g_lights[li];
    float u0, u1, v0, v1;
    if (!LightUvAabb(L.pos, L.range, u0, u1, v0, v1)) {
      continue;
    }
    if (u1 < 0.0 || u0 > 1.0 || v1 < 0.0 || v0 > 1.0) {
      continue;
    }
    // Overlap test in UV (unclamped AABB vs tile rect).
    if (u1 < tile_u0 || u0 > tile_u1 || v1 < tile_v0 || v0 > tile_v1) {
      continue;
    }
    if (count < (int)kMaxPerTile) {
      slots[count] = (int)li;
      ++count;
    }
  }

  g_tile_light_count[tile] = count;
  [unroll]
  for (int s = 0; s < 8; ++s) {
    g_tile_light_index[tile * kMaxPerTile + (uint)s] = slots[s];
  }
}
