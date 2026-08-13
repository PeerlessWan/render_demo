#pragma once

#include "engine/assets/image_loader.h"
#include "engine/core/result.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace engine::assets {

struct MeshVertex {
  float px = 0, py = 0, pz = 0;
  float nx = 0, ny = 1, nz = 0;
  float u = 0, v = 0;
};

struct GltfMeshAsset {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  ImageRgba8 albedo;
  ImageRgba8 orm;  // R=AO G=roughness B=metallic
  bool has_albedo = false;
  bool has_orm = false;
};

// Loads first mesh primitive from a .gltf/.glb (cgltf). Embedded images decoded via IImageLoader.
Result<GltfMeshAsset> LoadGltfMeshFile(const std::filesystem::path& path,
                                       const IImageLoader& images);

}  // namespace engine::assets
