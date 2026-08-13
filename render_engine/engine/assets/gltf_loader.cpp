#include "engine/assets/gltf_loader.h"

#include "engine/core/log.h"

#include <cmath>
#include <cstring>
#include <string>

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
#define CGLTF_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "cgltf.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif

namespace engine::assets {
namespace {

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF

Result<ImageRgba8> DecodeImageView(const cgltf_image& image, const IImageLoader& images) {
  if (image.buffer_view && image.buffer_view->buffer && image.buffer_view->buffer->data) {
    const auto* bytes = static_cast<const std::uint8_t*>(image.buffer_view->buffer->data) +
                        image.buffer_view->offset;
    const auto size = static_cast<std::size_t>(image.buffer_view->size);
    return images.LoadMemory(std::span<const std::uint8_t>(bytes, size));
  }
  if (image.uri && std::strncmp(image.uri, "data:", 5) == 0) {
    // Rare for DamagedHelmet GLB; skip data-URI here.
    return Result<ImageRgba8>::Fail("gltf data-URI images not supported");
  }
  return Result<ImageRgba8>::Fail("gltf image has no buffer view");
}

ImageRgba8 MrToOrm(const ImageRgba8& mr) {
  ImageRgba8 orm;
  orm.width = mr.width;
  orm.height = mr.height;
  orm.rgba.resize(mr.rgba.size());
  for (std::size_t i = 0; i + 3 < mr.rgba.size(); i += 4) {
    orm.rgba[i + 0] = 255;           // AO
    orm.rgba[i + 1] = mr.rgba[i + 1];  // roughness (G)
    orm.rgba[i + 2] = mr.rgba[i + 2];  // metallic (B)
    orm.rgba[i + 3] = 255;
  }
  return orm;
}

bool ReadAccessorFloats(const cgltf_accessor* acc, std::size_t elem_floats,
                        std::vector<float>& out) {
  if (!acc || !acc->buffer_view || !acc->buffer_view->buffer || !acc->buffer_view->buffer->data) {
    return false;
  }
  out.resize(static_cast<std::size_t>(acc->count) * elem_floats);
  for (cgltf_size i = 0; i < acc->count; ++i) {
    float tmp[4]{};
    if (!cgltf_accessor_read_float(acc, i, tmp, elem_floats)) {
      return false;
    }
    for (std::size_t c = 0; c < elem_floats; ++c) {
      out[static_cast<std::size_t>(i) * elem_floats + c] = tmp[c];
    }
  }
  return true;
}

Result<GltfMeshAsset> LoadWithCgltf(const std::filesystem::path& path, const IImageLoader& images) {
  cgltf_options options{};
  cgltf_data* data = nullptr;
  const std::string path_utf8 = path.string();
  cgltf_result res = cgltf_parse_file(&options, path_utf8.c_str(), &data);
  if (res != cgltf_result_success || !data) {
    return Result<GltfMeshAsset>::Fail("cgltf_parse_file failed");
  }
  res = cgltf_load_buffers(&options, data, path_utf8.c_str());
  if (res != cgltf_result_success) {
    cgltf_free(data);
    return Result<GltfMeshAsset>::Fail("cgltf_load_buffers failed");
  }

  if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
    cgltf_free(data);
    return Result<GltfMeshAsset>::Fail("gltf has no mesh primitives");
  }

  const cgltf_primitive& prim = data->meshes[0].primitives[0];
  const cgltf_accessor* pos = nullptr;
  const cgltf_accessor* nrm = nullptr;
  const cgltf_accessor* uv0 = nullptr;
  for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
    const cgltf_attribute& a = prim.attributes[i];
    if (a.type == cgltf_attribute_type_position) {
      pos = a.data;
    } else if (a.type == cgltf_attribute_type_normal) {
      nrm = a.data;
    } else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) {
      uv0 = a.data;
    }
  }
  if (!pos) {
    cgltf_free(data);
    return Result<GltfMeshAsset>::Fail("gltf primitive missing POSITION");
  }

  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<float> uvs;
  if (!ReadAccessorFloats(pos, 3, positions)) {
    cgltf_free(data);
    return Result<GltfMeshAsset>::Fail("failed to read POSITION");
  }
  if (nrm) {
    ReadAccessorFloats(nrm, 3, normals);
  }
  if (uv0) {
    ReadAccessorFloats(uv0, 2, uvs);
  }

  GltfMeshAsset out;
  out.vertices.resize(static_cast<std::size_t>(pos->count));
  for (std::size_t i = 0; i < out.vertices.size(); ++i) {
    MeshVertex v;
    v.px = positions[i * 3 + 0];
    v.py = positions[i * 3 + 1];
    v.pz = positions[i * 3 + 2];
    if (i * 3 + 2 < normals.size()) {
      v.nx = normals[i * 3 + 0];
      v.ny = normals[i * 3 + 1];
      v.nz = normals[i * 3 + 2];
    }
    if (i * 2 + 1 < uvs.size()) {
      v.u = uvs[i * 2 + 0];
      v.v = uvs[i * 2 + 1];
    }
    out.vertices[i] = v;
  }

  if (prim.indices) {
    out.indices.resize(static_cast<std::size_t>(prim.indices->count));
    for (cgltf_size i = 0; i < prim.indices->count; ++i) {
      out.indices[static_cast<std::size_t>(i)] =
          static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i));
    }
  } else {
    out.indices.resize(out.vertices.size());
    for (std::uint32_t i = 0; i < out.indices.size(); ++i) {
      out.indices[i] = i;
    }
  }

  if (prim.material) {
    const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
    if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
      if (auto img = DecodeImageView(*pbr.base_color_texture.texture->image, images)) {
        out.albedo = std::move(img.value());
        out.has_albedo = true;
      } else {
        LogError(std::string("gltf albedo decode: ") + img.status().message());
      }
    }
    if (pbr.metallic_roughness_texture.texture &&
        pbr.metallic_roughness_texture.texture->image) {
      if (auto img = DecodeImageView(*pbr.metallic_roughness_texture.texture->image, images)) {
        out.orm = MrToOrm(img.value());
        out.has_orm = true;
      } else {
        LogError(std::string("gltf MR decode: ") + img.status().message());
      }
    }
  }

  cgltf_free(data);
  LogInfo("gltf mesh loaded: " + std::to_string(out.vertices.size()) + " verts, " +
          std::to_string(out.indices.size()) + " indices");
  return Result<GltfMeshAsset>::Ok(std::move(out));
}

#endif  // ENGINE_WITH_CGLTF

}  // namespace

Result<GltfMeshAsset> LoadGltfMeshFile(const std::filesystem::path& path,
                                       const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
  return LoadWithCgltf(path, images);
#else
  (void)path;
  (void)images;
  return Result<GltfMeshAsset>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

}  // namespace engine::assets
