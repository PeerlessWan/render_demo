#include "engine/assets/gltf_loader.h"

#include "engine/core/log.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

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

Result<ImageRgba8> DecodeImageView(const cgltf_image& image, const IImageLoader& images,
                                   const std::filesystem::path& gltf_dir) {
  if (image.buffer_view && image.buffer_view->buffer && image.buffer_view->buffer->data) {
    const auto* bytes = static_cast<const std::uint8_t*>(image.buffer_view->buffer->data) +
                        image.buffer_view->offset;
    const auto size = static_cast<std::size_t>(image.buffer_view->size);
    return images.LoadMemory(std::span<const std::uint8_t>(bytes, size));
  }
  if (image.uri && std::strncmp(image.uri, "data:", 5) == 0) {
    return Result<ImageRgba8>::Fail("gltf data-URI images not supported");
  }
  if (image.uri && image.uri[0] != '\0') {
    const auto path = gltf_dir / image.uri;
    if (auto img = images.LoadFile(path)) {
      return img;
    }
    return Result<ImageRgba8>::Fail(std::string("gltf image uri load failed: ") + image.uri);
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

  // Apply the first node that references meshes[0] (DamagedHelmet etc. store upright
  // orientation on the node, not in POSITION). Falls back to identity.
  cgltf_float node_world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  bool have_node_xform = false;
  for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
    const cgltf_node& node = data->nodes[ni];
    if (node.mesh == &data->meshes[0]) {
      cgltf_node_transform_world(&node, node_world);
      have_node_xform = true;
      break;
    }
  }

  const cgltf_primitive& prim = data->meshes[0].primitives[0];
  const cgltf_accessor* pos = nullptr;
  const cgltf_accessor* nrm = nullptr;
  const cgltf_accessor* uv0 = nullptr;
  const cgltf_accessor* joints0 = nullptr;
  const cgltf_accessor* weights0 = nullptr;
  for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
    const cgltf_attribute& a = prim.attributes[i];
    if (a.type == cgltf_attribute_type_position) {
      pos = a.data;
    } else if (a.type == cgltf_attribute_type_normal) {
      nrm = a.data;
    } else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) {
      uv0 = a.data;
    } else if (a.type == cgltf_attribute_type_joints && a.index == 0) {
      joints0 = a.data;
    } else if (a.type == cgltf_attribute_type_weights && a.index == 0) {
      weights0 = a.data;
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
  // cgltf_node_transform_world is column-major, matching engine::Mat4.
  Mat4 xform = Mat4::Identity();
  if (have_node_xform) {
    for (int i = 0; i < 16; ++i) {
      xform.m[static_cast<std::size_t>(i)] = static_cast<float>(node_world[i]);
    }
  }
  for (std::size_t i = 0; i < out.vertices.size(); ++i) {
    MeshVertex v;
    const Vec3 p = xform.TransformPoint(
        Vec3{positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]});
    v.px = p.x;
    v.py = p.y;
    v.pz = p.z;
    if (i * 3 + 2 < normals.size()) {
      const Vec3 n = Normalize(xform.TransformVector(
          Vec3{normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]}));
      v.nx = n.x;
      v.ny = n.y;
      v.nz = n.z;
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

  const std::filesystem::path gltf_dir = path.parent_path();
  if (prim.material) {
    const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
    if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
      if (auto img = DecodeImageView(*pbr.base_color_texture.texture->image, images, gltf_dir)) {
        out.albedo = std::move(img.value());
        out.has_albedo = true;
      } else {
        LogError(std::string("gltf albedo decode: ") + img.status().message());
      }
    }
    if (pbr.metallic_roughness_texture.texture &&
        pbr.metallic_roughness_texture.texture->image) {
      if (auto img =
              DecodeImageView(*pbr.metallic_roughness_texture.texture->image, images, gltf_dir)) {
        out.orm = MrToOrm(img.value());
        out.has_orm = true;
      } else {
        LogError(std::string("gltf MR decode: ") + img.status().message());
      }
    }
  }

  // Minimal skin / joints (M6): first skin + JOINTS_0 / WEIGHTS_0 when present.
  if (data->skins_count > 0 && data->skins[0].joints_count > 0) {
    const cgltf_skin& skin = data->skins[0];
    out.skin.joints.resize(static_cast<std::size_t>(skin.joints_count));
    std::vector<float> ibm;
    if (skin.inverse_bind_matrices) {
      ReadAccessorFloats(skin.inverse_bind_matrices, 16, ibm);
    }
    for (cgltf_size ji = 0; ji < skin.joints_count; ++ji) {
      GltfJoint joint;
      const cgltf_node* node = skin.joints[ji];
      if (node && node->name) {
        joint.name = node->name;
      } else {
        joint.name = "joint_" + std::to_string(ji);
      }
      joint.parent = -1;
      if (node && node->parent) {
        for (cgltf_size pj = 0; pj < skin.joints_count; ++pj) {
          if (skin.joints[pj] == node->parent) {
            joint.parent = static_cast<int>(pj);
            break;
          }
        }
      }
      if (ji * 16 + 15 < ibm.size()) {
        for (int k = 0; k < 16; ++k) {
          joint.inverse_bind.m[static_cast<std::size_t>(k)] = ibm[ji * 16 + static_cast<std::size_t>(k)];
        }
      }
      out.skin.joints[static_cast<std::size_t>(ji)] = std::move(joint);
    }

    const std::size_t vcount = out.vertices.size();
    out.skin.vertex_joints.assign(vcount, {0, 0, 0, 0});
    out.skin.vertex_weights.assign(vcount, {1.f, 0.f, 0.f, 0.f});
    if (joints0 && weights0 && joints0->count == pos->count && weights0->count == pos->count) {
      for (cgltf_size i = 0; i < pos->count; ++i) {
        cgltf_uint jtmp[4]{};
        float wtmp[4]{};
        if (cgltf_accessor_read_uint(joints0, i, jtmp, 4)) {
          out.skin.vertex_joints[static_cast<std::size_t>(i)] = {
              static_cast<int>(jtmp[0]), static_cast<int>(jtmp[1]), static_cast<int>(jtmp[2]),
              static_cast<int>(jtmp[3])};
        }
        if (cgltf_accessor_read_float(weights0, i, wtmp, 4)) {
          out.skin.vertex_weights[static_cast<std::size_t>(i)] = {wtmp[0], wtmp[1], wtmp[2],
                                                                  wtmp[3]};
        }
      }
    }
    out.has_skin = !out.skin.joints.empty();
  }

  cgltf_free(data);
  LogInfo("gltf mesh loaded: " + std::to_string(out.vertices.size()) + " verts, " +
          std::to_string(out.indices.size()) + " indices" +
          (have_node_xform ? " (node world xform)" : " (raw mesh)") +
          (out.has_skin ? (", skin joints=" + std::to_string(out.skin.joints.size())) : ""));
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

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
Result<GltfMeshAsset> LoadGltfAllMeshNodesWithCgltf(const std::filesystem::path& path,
                                                    const IImageLoader& images) {
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
  if (data->meshes_count == 0) {
    cgltf_free(data);
    return Result<GltfMeshAsset>::Fail("gltf has no meshes");
  }

  GltfMeshAsset out;
  std::size_t parts = 0;
  for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
    const cgltf_node& node = data->nodes[ni];
    if (!node.mesh || node.mesh->primitives_count == 0) {
      continue;
    }
    cgltf_float node_world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    cgltf_node_transform_world(&node, node_world);
    Mat4 xform = Mat4::Identity();
    for (int i = 0; i < 16; ++i) {
      xform.m[static_cast<std::size_t>(i)] = static_cast<float>(node_world[i]);
    }

    const cgltf_primitive& prim = node.mesh->primitives[0];
    const cgltf_accessor* pos = nullptr;
    const cgltf_accessor* nrm = nullptr;
    const cgltf_accessor* uv0 = nullptr;
    for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
      const cgltf_attribute& a = prim.attributes[ai];
      if (a.type == cgltf_attribute_type_position) {
        pos = a.data;
      } else if (a.type == cgltf_attribute_type_normal) {
        nrm = a.data;
      } else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) {
        uv0 = a.data;
      }
    }
    if (!pos) {
      continue;
    }
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    if (!ReadAccessorFloats(pos, 3, positions)) {
      continue;
    }
    if (nrm) {
      ReadAccessorFloats(nrm, 3, normals);
    }
    if (uv0) {
      ReadAccessorFloats(uv0, 2, uvs);
    }

    GltfMeshAsset part;
    part.vertices.resize(static_cast<std::size_t>(pos->count));
    for (std::size_t i = 0; i < part.vertices.size(); ++i) {
      MeshVertex v;
      const Vec3 p = xform.TransformPoint(
          Vec3{positions[i * 3 + 0], positions[i * 3 + 1], positions[i * 3 + 2]});
      v.px = p.x;
      v.py = p.y;
      v.pz = p.z;
      if (i * 3 + 2 < normals.size()) {
        const Vec3 n = Normalize(xform.TransformVector(
            Vec3{normals[i * 3 + 0], normals[i * 3 + 1], normals[i * 3 + 2]}));
        v.nx = n.x;
        v.ny = n.y;
        v.nz = n.z;
      }
      if (i * 2 + 1 < uvs.size()) {
        v.u = uvs[i * 2 + 0];
        v.v = uvs[i * 2 + 1];
      }
      part.vertices[i] = v;
    }
    if (prim.indices) {
      part.indices.resize(static_cast<std::size_t>(prim.indices->count));
      for (cgltf_size ii = 0; ii < prim.indices->count; ++ii) {
        part.indices[static_cast<std::size_t>(ii)] =
            static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, ii));
      }
    } else {
      part.indices.resize(part.vertices.size());
      for (std::uint32_t ii = 0; ii < part.indices.size(); ++ii) {
        part.indices[ii] = ii;
      }
    }
    if (prim.material) {
      const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
      const std::filesystem::path gltf_dir = path.parent_path();
      if (!out.has_albedo && pbr.base_color_texture.texture &&
          pbr.base_color_texture.texture->image) {
        if (auto img =
                DecodeImageView(*pbr.base_color_texture.texture->image, images, gltf_dir)) {
          out.albedo = std::move(img.value());
          out.has_albedo = true;
        }
      }
    }
    AppendTransformedMesh(out, part, Mat4::Identity());
    ++parts;
  }

  cgltf_free(data);
  if (out.vertices.empty() || out.indices.empty()) {
    return Result<GltfMeshAsset>::Fail("gltf has no mesh-bearing nodes");
  }
  out.has_skin = false;
  LogInfo("gltf all-mesh-nodes: " + std::to_string(out.vertices.size()) + " verts, " +
          std::to_string(out.indices.size()) + " indices from " + std::to_string(parts) +
          " nodes");
  return Result<GltfMeshAsset>::Ok(std::move(out));
}
#endif

Result<GltfMeshAsset> LoadGltfAllMeshNodes(const std::filesystem::path& path,
                                           const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
  return LoadGltfAllMeshNodesWithCgltf(path, images);
#else
  (void)path;
  (void)images;
  return Result<GltfMeshAsset>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

void AppendTransformedMesh(GltfMeshAsset& dst, const GltfMeshAsset& src, const Mat4& world) {
  const std::uint32_t base = static_cast<std::uint32_t>(dst.vertices.size());
  dst.vertices.reserve(dst.vertices.size() + src.vertices.size());
  for (const auto& sv : src.vertices) {
    MeshVertex v = sv;
    const Vec3 p = world.TransformPoint(Vec3{sv.px, sv.py, sv.pz});
    const Vec3 n = Normalize(world.TransformVector(Vec3{sv.nx, sv.ny, sv.nz}));
    v.px = p.x;
    v.py = p.y;
    v.pz = p.z;
    v.nx = n.x;
    v.ny = n.y;
    v.nz = n.z;
    dst.vertices.push_back(v);
  }
  dst.indices.reserve(dst.indices.size() + src.indices.size());
  for (const auto idx : src.indices) {
    dst.indices.push_back(base + idx);
  }
  if (!dst.has_albedo && src.has_albedo) {
    dst.albedo = src.albedo;
    dst.has_albedo = true;
  }
  if (!dst.has_orm && src.has_orm) {
    dst.orm = src.orm;
    dst.has_orm = true;
  }
}

Result<GltfMeshAsset> AssembleGltfMeshes(const std::vector<GltfMeshInstance>& instances,
                                         const IImageLoader& images) {
  GltfMeshAsset out;
  for (const auto& inst : instances) {
    auto loaded = LoadGltfMeshFile(inst.path, images);
    if (!loaded) {
      return Result<GltfMeshAsset>::Fail(std::string("AssembleGltfMeshes: ") +
                                         loaded.status().message() + " (" +
                                         inst.path.string() + ")");
    }
    AppendTransformedMesh(out, loaded.value(), inst.world);
  }
  if (out.vertices.empty() || out.indices.empty()) {
    return Result<GltfMeshAsset>::Fail("AssembleGltfMeshes: empty");
  }
  LogInfo("Assembled glTF scene: " + std::to_string(out.vertices.size()) + " verts, " +
          std::to_string(out.indices.size()) + " indices from " +
          std::to_string(instances.size()) + " parts");
  return Result<GltfMeshAsset>::Ok(std::move(out));
}

}  // namespace engine::assets
