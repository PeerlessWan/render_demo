#pragma once

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include "game_kit/scene_document.h"
#include "game_kit/script_component.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game_kit {
class GameRuntime;
}

namespace editor {

struct NodeMeta {
  std::string prefab_id;
  std::string script_path;
  std::string material_id;
  std::string script_fields;
  std::string anim_state;
  std::string source_prefab;
  std::string override_json;
  bool has_light = false;
  float light_range = 8.f;
  float light_intensity = 1.f;
  bool has_camera = false;
  bool active_camera = false;
  bool has_collider = false;
  float collider_hx = 0.5f;
  float collider_hy = 0.5f;
  float collider_hz = 0.5f;
};

std::string EncodeNodeMeta(const NodeMeta& m);
void DecodeNodeMetaExtra(std::string_view extra, NodeMeta* m);

void CollectAllNodes(const engine::scene::World& world, std::vector<engine::scene::NodeId>* out);

[[nodiscard]] engine::scene::NodeId FindNamed(const engine::scene::World& world,
                                              std::string_view name);

void StampMeta(game_kit::SceneDocument* doc,
               const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);

void RestoreMeta(const game_kit::SceneDocument& stored, const game_kit::SceneDocument& captured,
                 std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

void SyncMetaToWorld(engine::scene::World& world,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);
void SyncWorldToMeta(const engine::scene::World& world,
                     std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

void BindPlayScripts(game_kit::ScriptComponentWorld& scripts, engine::scene::World& world,
                     game_kit::GameRuntime& rt,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);

[[nodiscard]] float GroundTopY(const engine::scene::World& world, float x, float z);

void ResolveGroundY(engine::scene::World& world, engine::scene::NodeId player);

void MovePlayerOnGround(engine::scene::World& world, engine::scene::NodeId player, float yaw,
                        const engine::Vec3& wish, float speed, float dt);

void FollowPlayerCamera(engine::render::Camera* cam, const engine::Vec3& player_pos);

}  // namespace editor
