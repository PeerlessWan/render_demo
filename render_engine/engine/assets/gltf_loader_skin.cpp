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

Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshPartsWithCgltf(
        const std::filesystem::path& path, const IImageLoader& images) {
    // Prefer full LoadGltfMeshFile per mesh index when skins exist — multi-draw, keep skin.
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string path_utf8 = path.string();
    if (cgltf_parse_file(&options, path_utf8.c_str(), &data) != cgltf_result_success || !data) {
        return Result<std::vector<GltfMeshAsset>>::Fail("cgltf_parse_file failed");
    }
    if (cgltf_load_buffers(&options, data, path_utf8.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return Result<std::vector<GltfMeshAsset>>::Fail("cgltf_load_buffers failed");
    }
    if (data->skins_count == 0 || data->meshes_count == 0) {
        cgltf_free(data);
        return Result<std::vector<GltfMeshAsset>>::Fail("gltf has no skin/meshes");
    }

    std::vector<GltfMeshAsset> parts;
    // Reuse single-mesh loader for mesh0; additional meshes: merge their prims with same skin
    // by calling LoadGltfMeshFile once then splitting is hard — load via all-nodes static then
    // attach skin joints from mesh0 for each node mesh that has JOINTS.
    auto first = LoadWithCgltf(path, images);
    if (!first || !first->has_skin) {
        cgltf_free(data);
        return Result<std::vector<GltfMeshAsset>>::Fail("skinned mesh0 load failed");
    }
    parts.push_back(std::move(first.value()));

    // Extra mesh-bearing nodes beyond meshes[0]: emit separate draw parts with shared joints.
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        if (!node.mesh || node.mesh == &data->meshes[0] || node.mesh->primitives_count == 0) {
            continue;
        }
        GltfMeshAsset part;
        part.has_skin = true;
        part.skin = parts.front().skin;  // shared inverse-binds / hierarchy
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
            const cgltf_accessor* joints0 = nullptr;
            const cgltf_accessor* weights0 = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
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
            GltfMeshAsset prim_mesh;
            prim_mesh.vertices.resize(static_cast<std::size_t>(pos->count));
            prim_mesh.skin.vertex_joints.resize(prim_mesh.vertices.size());
            prim_mesh.skin.vertex_weights.resize(prim_mesh.vertices.size());
            for (std::size_t i = 0; i < prim_mesh.vertices.size(); ++i) {
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
                prim_mesh.vertices[i] = v;
                std::array<int, 4> j{0, 0, 0, 0};
                std::array<float, 4> w{1.f, 0.f, 0.f, 0.f};
                if (joints0 && weights0) {
                    float jf[4]{};
                    float wf[4]{};
                    cgltf_accessor_read_float(joints0, i, jf, 4);
                    cgltf_accessor_read_float(weights0, i, wf, 4);
                    for (int k = 0; k < 4; ++k) {
                        j[static_cast<std::size_t>(k)] = static_cast<int>(jf[k]);
                        w[static_cast<std::size_t>(k)] = wf[k];
                    }
                }
                prim_mesh.skin.vertex_joints[i] = j;
                prim_mesh.skin.vertex_weights[i] = w;
            }
            if (prim.indices) {
                prim_mesh.indices.resize(static_cast<std::size_t>(prim.indices->count));
                for (cgltf_size ii = 0; ii < prim.indices->count; ++ii) {
                    prim_mesh.indices[static_cast<std::size_t>(ii)] =
                            static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, ii));
                }
            } else {
                prim_mesh.indices.resize(prim_mesh.vertices.size());
                for (std::uint32_t ii = 0; ii < prim_mesh.indices.size(); ++ii) {
                    prim_mesh.indices[ii] = ii;
                }
            }
            AppendTransformedMesh(part, prim_mesh, Mat4::Identity());
            // Preserve per-vertex skin after append (AppendTransformedMesh drops skin).
            const std::size_t base = part.skin.vertex_joints.size();
            part.skin.vertex_joints.insert(part.skin.vertex_joints.end(),
                                                                         prim_mesh.skin.vertex_joints.begin(),
                                                                         prim_mesh.skin.vertex_joints.end());
            part.skin.vertex_weights.insert(part.skin.vertex_weights.end(),
                                                                            prim_mesh.skin.vertex_weights.begin(),
                                                                            prim_mesh.skin.vertex_weights.end());
            (void)base;
        }
        if (!part.vertices.empty()) {
            part.has_skin = true;
            part.skin.joints = parts.front().skin.joints;
            parts.push_back(std::move(part));
        }
    }

    cgltf_free(data);
    if (parts.empty()) {
        return Result<std::vector<GltfMeshAsset>>::Fail("no skinned parts");
    }
    LogInfo("gltf skinned mesh parts: " + std::to_string(parts.size()) + " draws");
    return Result<std::vector<GltfMeshAsset>>::Ok(std::move(parts));
}

#endif

}  // namespace gltf_detail

Result<std::vector<GltfMeshAsset>> LoadGltfSkinnedMeshParts(const std::filesystem::path& path,
                                                                                                                        const IImageLoader& images) {
#if defined(ENGINE_WITH_CGLTF) && ENGINE_WITH_CGLTF
    return gltf_detail::LoadGltfSkinnedMeshPartsWithCgltf(path, images);
#else
    (void)path;
    (void)images;
    return Result<std::vector<GltfMeshAsset>>::Fail("ENGINE_WITH_CGLTF=0");
#endif
}

}  // namespace engine::assets
