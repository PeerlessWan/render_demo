#pragma once

#include "engine/render/camera.h"
#include "engine/scene/world.h"

#include "game_kit/scene_document.h"
#include "game_kit/script_component.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace game_kit {
class GameRuntime;
}

namespace editor {

struct NodeMeta {
  std::string prefab_id;
  std::string script_path;
};

[[nodiscard]] engine::scene::NodeId FindNamed(const engine::scene::World& world,
                                              std::string_view name);

void StampMeta(game_kit::SceneDocument* doc,
               const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);

void RestoreMeta(const game_kit::SceneDocument& stored, const game_kit::SceneDocument& captured,
                 std::unordered_map<engine::scene::NodeId, NodeMeta>* meta);

void BindPlayScripts(game_kit::ScriptComponentWorld& scripts, engine::scene::World& world,
                     game_kit::GameRuntime& rt,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta);

void MovePlayerOnGround(engine::scene::World& world, engine::scene::NodeId player, float yaw,
                        const engine::Vec3& wish, float speed, float dt);

void FollowPlayerCamera(engine::render::Camera* cam, const engine::Vec3& player_pos);

}  // namespace editor
