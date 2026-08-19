#pragma once

#include "engine/assets/gltf_loader.h"
#include "engine/assets/image_loader.h"

#include <filesystem>
#include <string>

namespace engine::assets {

// Mega-W11 / ADR 0038: prefer CC0 humanoid glTF under content/characters/,
// else procedural capsule mesh (same topology as BuildCapsuleCharacterMesh).
struct CharacterLoadResult {
  bool used_capsule_fallback = false;
  bool has_skin = false;
  std::filesystem::path source_path;  // glTF path when loaded; empty on capsule
  std::string note;                   // short diagnostic for samples/logs
  GltfMeshAsset mesh;
};

struct CharacterAsset {
  // Load one .gltf/.glb. Static multi-mesh files merge all mesh nodes; skinned
  // files keep mesh0 + joints. On missing/invalid file, fill mesh from a standing
  // capsule (feet on y=0) and set used_capsule_fallback.
  [[nodiscard]] static CharacterLoadResult TryLoadGltfOrCapsule(
      const std::filesystem::path& gltf_or_glb_path, const IImageLoader& images,
      float capsule_radius = 0.35f, float capsule_height = 1.8f);

  // Prefer kenney_blocky_character.glb, else first *.glb / *.gltf in dir; else capsule.
  [[nodiscard]] static CharacterLoadResult TryLoadFromCharactersDirOrCapsule(
      const std::filesystem::path& characters_dir, const IImageLoader& images,
      float capsule_radius = 0.35f, float capsule_height = 1.8f);
};

}  // namespace engine::assets
