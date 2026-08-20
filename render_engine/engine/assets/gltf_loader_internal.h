#pragma once

#include "engine/assets/gltf_loader.h"

#include <filesystem>
#include <vector>

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
struct cgltf_image;
struct cgltf_accessor;
#endif

namespace engine::assets {
namespace gltf_detail {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
Result<ImageRgba8> DecodeImageView(const cgltf_image& image, const IImageLoader& images,
                                                                     const std::filesystem::path& gltf_dir);
ImageRgba8 MrToOrm(const ImageRgba8& mr);
bool ReadAccessorFloats(const cgltf_accessor* acc, std::size_t elem_floats,
                                                std::vector<float>& out);
Result<GltfMeshAsset> LoadWithCgltf(const std::filesystem::path& path, const IImageLoader& images);
Result<GltfMeshAsset> LoadGltfAllMeshNodesWithCgltf(const std::filesystem::path& path,
                                                                                                        const IImageLoader& images);
Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshPartsWithCgltf(
        const std::filesystem::path& path, const IImageLoader& images);
#endif

}  // namespace gltf_detail
}  // namespace engine::assets
