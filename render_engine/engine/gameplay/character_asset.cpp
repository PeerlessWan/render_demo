#include "engine/assets/character_asset.h"

#include "engine/gameplay/possess_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace engine::assets {
namespace {

GltfMeshAsset MeshFromCapsule(float radius, float height) {
  const auto capsule = gameplay::BuildCapsuleCharacterMesh(radius, height, 8, 12);
  GltfMeshAsset out;
  out.vertices.resize(capsule.positions.size());
  const float mid_y = 0.5f * height;
  for (std::size_t i = 0; i < capsule.positions.size(); ++i) {
    const auto& p = capsule.positions[i];
    MeshVertex v;
    v.px = p.x;
    v.py = p.y;
    v.pz = p.z;
    const float nx = p.x;
    const float ny = p.y - mid_y;
    const float nz = p.z;
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-5f) {
      v.nx = nx / len;
      v.ny = ny / len;
      v.nz = nz / len;
    } else {
      v.nx = 0.f;
      v.ny = 1.f;
      v.nz = 0.f;
    }
    v.u = 0.5f;
    v.v = height > 1e-5f ? std::clamp(p.y / height, 0.f, 1.f) : 0.f;
    out.vertices[i] = v;
  }
  out.indices = capsule.indices;
  out.has_skin = false;
  return out;
}

CharacterLoadResult MakeCapsuleFallback(float radius, float height, std::string note) {
  CharacterLoadResult r;
  r.used_capsule_fallback = true;
  r.has_skin = false;
  r.note = std::move(note);
  r.mesh = MeshFromCapsule(radius, height);
  return r;
}

}  // namespace

CharacterLoadResult CharacterAsset::TryLoadGltfOrCapsule(const std::filesystem::path& gltf_or_glb_path,
                                                        const IImageLoader& images,
                                                        float capsule_radius,
                                                        float capsule_height) {
  if (gltf_or_glb_path.empty() || !std::filesystem::exists(gltf_or_glb_path)) {
    return MakeCapsuleFallback(capsule_radius, capsule_height,
                               "missing path; capsule fallback");
  }

  // Skinned: keep mesh0 + joints. Static multi-mesh (Kenney blocky): merge all
  // mesh-bearing nodes — LoadGltfMeshFile alone only kept the left leg.
  auto first = LoadGltfMeshFile(gltf_or_glb_path, images);
  if (!first) {
    return MakeCapsuleFallback(capsule_radius, capsule_height,
                               std::string("gltf load failed: ") + first.status().message());
  }

  CharacterLoadResult r;
  r.used_capsule_fallback = false;
  r.source_path = gltf_or_glb_path;
  if (first->has_skin) {
    r.mesh = std::move(first.value());
  } else {
    auto merged = LoadGltfAllMeshNodes(gltf_or_glb_path, images);
    if (merged && merged->vertices.size() >= first->vertices.size()) {
      r.mesh = std::move(merged.value());
    } else {
      r.mesh = std::move(first.value());
    }
  }
  r.has_skin = r.mesh.has_skin;
  r.note = std::string("loaded ") + gltf_or_glb_path.filename().string() +
           (r.has_skin ? " (skinned)" : " (static multi-mesh)");
  return r;
}

CharacterLoadResult CharacterAsset::TryLoadFromCharactersDirOrCapsule(
    const std::filesystem::path& characters_dir, const IImageLoader& images, float capsule_radius,
    float capsule_height) {
  namespace fs = std::filesystem;
  if (characters_dir.empty() || !fs::exists(characters_dir) || !fs::is_directory(characters_dir)) {
    return MakeCapsuleFallback(capsule_radius, capsule_height,
                               "characters dir missing; capsule fallback");
  }

  const fs::path preferred = characters_dir / "kenney_blocky_character.glb";
  if (fs::exists(preferred)) {
    return TryLoadGltfOrCapsule(preferred, images, capsule_radius, capsule_height);
  }

  std::vector<fs::path> candidates;
  for (const auto& entry : fs::directory_iterator(characters_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto ext = entry.path().extension().string();
    std::string lower = ext;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == ".glb" || lower == ".gltf") {
      candidates.push_back(entry.path());
    }
  }
  std::sort(candidates.begin(), candidates.end());
  if (!candidates.empty()) {
    return TryLoadGltfOrCapsule(candidates.front(), images, capsule_radius, capsule_height);
  }

  return MakeCapsuleFallback(capsule_radius, capsule_height,
                             "no *.glb/*.gltf in characters dir; capsule fallback");
}

}  // namespace engine::assets
