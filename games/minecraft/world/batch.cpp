#include "world/batch.h"

#include "sim/blocks.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace mc {
namespace {

engine::rhi::LitDrawItem MakeCube(int x, int y, int z, const Def& d, bool trans) {
  engine::rhi::LitDrawItem item;
  item.world = engine::Mat4::TRS(engine::Vec3{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f,
                                              static_cast<float>(z) + 0.5f},
                                 engine::Quat::Identity(), engine::Vec3{1.f, 1.f, 1.f});
  item.color = d.color;
  item.use_albedo = false;
  item.use_orm = false;
  item.mesh_slot = 0;
  item.metallic = 0.02f;
  item.roughness = 0.85f;
  item.transparent = trans;
  return item;
}

bool Exposed(const World& w, int x, int y, int z) {
  static const int kN[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (const auto& n : kN) {
    const Id nb = w.Get(x + n[0], y + n[1], z + n[2]);
    if (!IsSolid(nb) || IsTransparent(nb)) {
      return true;
    }
  }
  return false;
}

}  // namespace

void CollectVisible(const World& world, const engine::Vec3& eye, int radius,
                    std::vector<engine::rhi::LitDrawItem>* opaque,
                    std::vector<engine::rhi::LitDrawItem>* water) {
  if (!opaque || !water) {
    return;
  }
  opaque->clear();
  water->clear();
  ChunkCoord center{};
  WorldToChunk(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)), &center,
               nullptr, nullptr);

  struct Cand {
    int x = 0;
    int y = 0;
    int z = 0;
    Id id = Id::Air;
    float d2 = 0.f;
    bool surface = false;
  };
  std::vector<Cand> cands;
  cands.reserve(8192);
  const float cave_lim = 18.f * 18.f;
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const Chunk* ch = world.FindChunk(ChunkCoord{center.x + dx, center.z + dz});
      if (!ch) {
        continue;
      }
      const int ox = (center.x + dx) * kChunkW;
      const int oz = (center.z + dz) * kChunkW;
      for (int y = kChunkH - 1; y >= 0; --y) {
        for (int lz = 0; lz < kChunkW; ++lz) {
          for (int lx = 0; lx < kChunkW; ++lx) {
            const Id id = ch->Get(lx, y, lz);
            if (id == Id::Air) {
              continue;
            }
            const int x = ox + lx;
            const int z = oz + lz;
            if (!Exposed(world, x, y, z)) {
              continue;
            }
            const Id above = world.Get(x, y + 1, z);
            const bool surface = !IsSolid(above) || IsTransparent(above);
            const float dxw = static_cast<float>(x) + 0.5f - eye.x;
            const float dyw = static_cast<float>(y) + 0.5f - eye.y;
            const float dzw = static_cast<float>(z) + 0.5f - eye.z;
            const float d2 = dxw * dxw + dyw * dyw + dzw * dzw;
            if (!surface && d2 > cave_lim) {
              continue;
            }
            cands.push_back(Cand{x, y, z, id, d2, surface});
          }
        }
      }
    }
  }

  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) {
    if (a.surface != b.surface) {
      return a.surface && !b.surface;
    }
    return a.d2 < b.d2;
  });

  constexpr int kMax = 14000;
  opaque->reserve(static_cast<std::size_t>((std::min)(kMax, static_cast<int>(cands.size()))));
  for (const Cand& c : cands) {
    if (static_cast<int>(opaque->size() + water->size()) >= kMax) {
      break;
    }
    const auto& d = GetDef(c.id);
    if (c.id == Id::Water) {
      water->push_back(MakeCube(c.x, c.y, c.z, d, true));
    } else {
      opaque->push_back(MakeCube(c.x, c.y, c.z, d, false));
    }
  }
}

namespace {

std::uint32_t ColorKey(const engine::ColorRgba& c) {
  auto q = [](float v) {
    const int i = static_cast<int>(v * 255.f + 0.5f);
    return static_cast<std::uint32_t>(i < 0 ? 0 : (i > 255 ? 255 : i));
  };
  return (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

}  // namespace

void PrepareLitSubmit(const std::vector<engine::rhi::LitDrawItem>& opaque, const engine::Vec3& eye,
                      LitSubmit* out) {
  if (!out) {
    return;
  }
  out->instanced_proto = {};
  out->instanced_proto.use_albedo = false;
  out->instanced_proto.mesh_slot = 0;
  out->instanced_worlds.clear();
  out->colored.clear();
  if (opaque.empty()) {
    return;
  }

  std::unordered_map<std::uint32_t, int> counts;
  counts.reserve(opaque.size());
  for (const auto& it : opaque) {
    ++counts[ColorKey(it.color)];
  }
  std::uint32_t best = ColorKey(opaque[0].color);
  int best_n = 0;
  for (const auto& kv : counts) {
    if (kv.second > best_n) {
      best_n = kv.second;
      best = kv.first;
    }
  }

  std::vector<const engine::rhi::LitDrawItem*> others;
  others.reserve(opaque.size());
  for (const auto& it : opaque) {
    if (ColorKey(it.color) == best) {
      if (out->instanced_worlds.empty()) {
        out->instanced_proto = it;
        out->instanced_proto.use_albedo = false;
        out->instanced_proto.mesh_slot = 0;
        out->instanced_proto.transparent = false;
      }
      out->instanced_worlds.push_back(it.world);
    } else {
      others.push_back(&it);
    }
  }

  std::sort(others.begin(), others.end(), [&](const engine::rhi::LitDrawItem* a,
                                              const engine::rhi::LitDrawItem* b) {
    const engine::Vec3 pa{a->world.m[12], a->world.m[13], a->world.m[14]};
    const engine::Vec3 pb{b->world.m[12], b->world.m[13], b->world.m[14]};
    const engine::Vec3 da = pa - eye;
    const engine::Vec3 db = pb - eye;
    return da.length_squared() < db.length_squared();
  });

  // Slot 63 is reserved for DrawLitInstanced object CB.
  constexpr int kAccent = 62;
  for (const auto* it : others) {
    if (static_cast<int>(out->colored.size()) < kAccent) {
      out->colored.push_back(*it);
    } else {
      out->instanced_worlds.push_back(it->world);
    }
  }
}

}  // namespace mc
