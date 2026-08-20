#pragma once

#include "engine/assets/image_loader.h"
#include "engine/core/math.h"
#include "engine/core/result.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace engine::assets {

struct MeshVertex {
  float px = 0, py = 0, pz = 0;
  float nx = 0, ny = 1, nz = 0;
  float u = 0, v = 0;
};

// Minimal glTF skin / joints (M6). Inverse-bind matrices feed animation::Skeleton.
struct GltfJoint {
  std::string name;
  int parent = -1;
  Mat4 inverse_bind = Mat4::Identity();
};

struct GltfSkinData {
  std::vector<GltfJoint> joints;
  // Parallel to mesh vertices when has_skin; else empty.
  std::vector<std::array<int, 4>> vertex_joints;
  std::vector<std::array<float, 4>> vertex_weights;
};

struct GltfMeshAsset {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint32_t> indices;
  ImageRgba8 albedo;
  ImageRgba8 orm;  // R=AO G=roughness B=metallic
  bool has_albedo = false;
  bool has_orm = false;
  GltfSkinData skin;
  bool has_skin = false;
};

// Loads first mesh primitive from a .gltf/.glb (cgltf). Embedded images decoded via IImageLoader.
// When the file contains a skin referencing that mesh, fills skin + has_skin.
Result<GltfMeshAsset> LoadGltfMeshFile(const std::filesystem::path& path,
                                       const IImageLoader& images);

// Merge every mesh-bearing node's primitives (with node world xform).
// For modular characters (Kenney blocky: legs/torso/arms/head as separate meshes).
// Skinned multi-draw: use LoadGltfSkinnedMeshParts (preserves has_skin per part).
Result<GltfMeshAsset> LoadGltfAllMeshNodes(const std::filesystem::path& path,
                                           const IImageLoader& images);

// W16 ADR 0040: one GltfMeshAsset per mesh-bearing node (all prims merged per node),
// each keeps skin/joints. Do not merge across nodes (would invalidate skin).
Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshParts(const std::filesystem::path& path,
                                                            const IImageLoader& images);

// Append src into dst with a world transform (positions + normals).
void AppendTransformedMesh(GltfMeshAsset& dst, const GltfMeshAsset& src, const Mat4& world);

struct GltfMeshInstance {
  std::filesystem::path path;
  Mat4 world = Mat4::Identity();
};

// Load many glTF/GLB parts and merge into one mesh (for modular CC0 kits / mini scenes).
Result<GltfMeshAsset> AssembleGltfMeshes(const std::vector<GltfMeshInstance>& instances,
                                         const IImageLoader& images);

}  // namespace engine::assets
