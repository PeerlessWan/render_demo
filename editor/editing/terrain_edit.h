#pragma once

#include "engine/rhi/i_device.h"
#include "engine/scene/world.h"
#include "engine/terrain/heightmap.h"

#include <vector>

namespace editor {

inline constexpr int kSculptRes = 17;

void EnsureHeights(std::vector<float>* heights);

void RaiseHeight(std::vector<float>* heights, int x, int z, float amount, float radius);
void LowerHeight(std::vector<float>* heights, int x, int z, float amount, float radius);
void SmoothHeight(std::vector<float>* heights, int x, int z, float radius);

engine::terrain::Heightmap HeightsToMap(const std::vector<float>& heights, float cell = 1.f);

bool UploadTerrainMesh(engine::rhi::IDevice& device, engine::scene::World& world,
                       const std::vector<float>& heights, int mesh_slot = 2);

}  // namespace editor
