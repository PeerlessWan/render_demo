#include "engine/assets/gltf_loader.h"
#include "engine/assets/gltf_loader_internal.h"

#include "engine/core/log.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
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
namespace gltf_detail {

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

        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node.mesh->primitives[pi];
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
    }

    cgltf_free(data);
    if (out.vertices.empty() || out.indices.empty()) {
        return Result<GltfMeshAsset>::Fail("gltf has no mesh-bearing nodes");
    }
    out.has_skin = false;
    LogInfo("gltf all-mesh-nodes: " + std::to_string(out.vertices.size()) + " verts, " +
                    std::to_string(out.indices.size()) + " indices from " + std::to_string(parts) +
                    " prims");
    return Result<GltfMeshAsset>::Ok(std::move(out));
}

#endif

}  // namespace gltf_detail

Result<GltfMeshAsset> LoadGltfAllMeshNodes(const std::filesystem::path& path,
                                                                                     const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
    return gltf_detail::LoadGltfAllMeshNodesWithCgltf(path, images);
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
