// Mega-W10 C02: Forward+ light → screen-tile × Z-slice cull CS.
// Layout matches FrameCB: tile_light_count[128], tile_light_index[1024]
// (grid 8×4×4, ≤8 lights/cluster, ≤32 lights). Range sphere → UV AABB via center + ±range XYZ;
// view-Z via eye + cam_forward (center±range) for coarse slices.
// CPU reference: engine::render::AssignLightsToTiles / SimulateLightTileCullCs.

static const uint kGridW = 8;
static const uint kGridH = 4;
static const uint kTileCount = 32;
static const uint kZSlices = 4;
static const uint kClusterCount = 128;
static const uint kMaxLights = 32;
static const uint kMaxPerTile = 8;

struct LightPacked {
  float3 pos;
  float range;
};

cbuffer TileCullCB : register(b0) {
  float4x4 view_proj;
  float3 eye;
  uint light_count;
  float3 cam_forward;
  float z_near;
  float z_far;
  uint _pad0;
  uint _pad1;
  uint _pad2;
};

StructuredBuffer<LightPacked> g_lights : register(t0);
RWStructuredBuffer<int> g_tile_light_count : register(u0);   // [128]
RWStructuredBuffer<int> g_tile_light_index : register(u1);   // [1024]

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

int ViewZToSlice(float view_z) {
  float denom = max(z_far - z_near, 1e-5);
  float t = saturate((view_z - z_near) / denom);
  t = min(t, 0.999);
  return min((int)(t * (float)kZSlices), (int)kZSlices - 1);
}

[numthreads(8, 4, 4)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
  if (dtid.x >= kGridW || dtid.y >= kGridH || dtid.z >= kZSlices) {
    return;
  }
  const uint tile = dtid.y * kGridW + dtid.x;
  const uint slice = dtid.z;
  const uint cluster = slice * kTileCount + tile;
  const float tile_u0 = (float)dtid.x / (float)kGridW;
  const float tile_u1 = (float)(dtid.x + 1) / (float)kGridW;
  const float tile_v0 = (float)dtid.y / (float)kGridH;
  const float tile_v1 = (float)(dtid.y + 1) / (float)kGridH;

  float3 fwd = cam_forward;
  float fl = length(fwd);
  if (fl < 1e-6) {
    fwd = float3(0, 0, -1);
  } else {
    fwd /= fl;
  }

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
    float r = max(L.range, 0.0);
    float vz = dot(L.pos - eye, fwd);
    int sz0 = ViewZToSlice(vz - r);
    int sz1 = ViewZToSlice(vz + r);
    if ((int)slice < sz0 || (int)slice > sz1) {
      continue;
    }
    if (count < (int)kMaxPerTile) {
      slots[count] = (int)li;
      ++count;
    }
  }

  g_tile_light_count[cluster] = count;
  [unroll]
  for (int s = 0; s < 8; ++s) {
    g_tile_light_index[cluster * kMaxPerTile + (uint)s] = slots[s];
  }
}
