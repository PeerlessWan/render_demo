#include "editing/light_bake.h"

#include "play/scene_play.h"

#include "engine/core/result.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace editor {

engine::Status BakeSceneLights(const engine::scene::World& world, const std::vector<float>& heights,
                               const std::filesystem::path& out_rgba, engine::rhi::IDevice* device) {
  constexpr int kW = 64;
  constexpr int kH = 64;
  std::vector<engine::scene::NodeId> nodes;
  CollectAllNodes(world, &nodes);
  struct Lamp {
    engine::Vec3 pos{};
    engine::Vec3 color{1.f, 0.95f, 0.85f};
    float intensity = 1.f;
    float range = 8.f;
  };
  std::vector<Lamp> lamps;
  for (auto id : nodes) {
    const auto* L = world.light(id);
    if (!L) {
      continue;
    }
    const auto& m = world.world_matrix(id);
    lamps.push_back(Lamp{{m.m[12], m.m[13], m.m[14]}, L->color, L->intensity, L->range});
  }
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(kW * kH * 4), 0);
  for (int z = 0; z < kH; ++z) {
    for (int x = 0; x < kW; ++x) {
      const float wx = (static_cast<float>(x) / static_cast<float>(kW - 1)) * 16.f - 8.f;
      const float wz = (static_cast<float>(z) / static_cast<float>(kH - 1)) * 16.f - 8.f;
      float hy = 0.f;
      if (heights.size() == 17u * 17u) {
        const int hx = std::clamp(static_cast<int>(wx + 8.f), 0, 16);
        const int hz = std::clamp(static_cast<int>(wz + 8.f), 0, 16);
        hy = heights[static_cast<std::size_t>(hz * 17 + hx)];
      }
      const engine::Vec3 p{wx, hy, wz};
      const engine::Vec3 n{0.f, 1.f, 0.f};
      engine::Vec3 acc{0.12f, 0.12f, 0.14f};
      for (const auto& lamp : lamps) {
        engine::Vec3 to = lamp.pos - p;
        const float dist = to.length();
        if (dist < 1e-4f || dist > lamp.range) {
          continue;
        }
        to = engine::Normalize(to);
        const float ndl = std::max(0.f, engine::Dot(n, to));
        const float att = 1.f - dist / lamp.range;
        acc.x += lamp.color.x * lamp.intensity * ndl * att;
        acc.y += lamp.color.y * lamp.intensity * ndl * att;
        acc.z += lamp.color.z * lamp.intensity * ndl * att;
      }
      const std::size_t i = static_cast<std::size_t>((z * kW + x) * 4);
      rgba[i + 0] = static_cast<std::uint8_t>(std::clamp(acc.x, 0.f, 1.f) * 255.f);
      rgba[i + 1] = static_cast<std::uint8_t>(std::clamp(acc.y, 0.f, 1.f) * 255.f);
      rgba[i + 2] = static_cast<std::uint8_t>(std::clamp(acc.z, 0.f, 1.f) * 255.f);
      rgba[i + 3] = 255;
    }
  }
  std::error_code ec;
  if (!out_rgba.parent_path().empty()) {
    std::filesystem::create_directories(out_rgba.parent_path(), ec);
  }
  std::ofstream out(out_rgba, std::ios::binary);
  if (!out) {
    return engine::Status::Fail("cannot write lightmap");
  }
  const std::uint32_t wh[2] = {static_cast<std::uint32_t>(kW), static_cast<std::uint32_t>(kH)};
  out.write(reinterpret_cast<const char*>(wh), sizeof(wh));
  out.write(reinterpret_cast<const char*>(rgba.data()), static_cast<std::streamsize>(rgba.size()));
  if (!out) {
    return engine::Status::Fail("lightmap write failed");
  }
  if (device) {
    if (auto st = device->UploadLitAlbedoRgba(rgba.data(), kW, kH, 0); !st) {
      return st;
    }
  }
  return engine::Status::Ok();
}

}  // namespace editor
