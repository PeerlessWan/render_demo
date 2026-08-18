#include "play/scene_play.h"

#include "engine/core/math.h"

#include "game_kit/runtime.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace editor {
namespace {

void Collect(const engine::scene::World& world, engine::scene::NodeId id,
             std::vector<engine::scene::NodeId>* out) {
  if (!out || !world.valid(id)) {
    return;
  }
  out->push_back(id);
  for (auto c : world.children(id)) {
    Collect(world, c, out);
  }
}

void AllNodes(const engine::scene::World& world, std::vector<engine::scene::NodeId>* out) {
  if (!out) {
    return;
  }
  out->clear();
  for (auto r : world.roots()) {
    Collect(world, r, out);
  }
}

}  // namespace

engine::scene::NodeId FindNamed(const engine::scene::World& world, std::string_view name) {
  std::vector<engine::scene::NodeId> nodes;
  AllNodes(world, &nodes);
  for (auto id : nodes) {
    if (world.name(id) == name) {
      return id;
    }
  }
  return engine::scene::kInvalidNode;
}

void StampMeta(game_kit::SceneDocument* doc,
               const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  if (!doc) {
    return;
  }
  for (auto& n : doc->nodes) {
    const auto id = static_cast<engine::scene::NodeId>(std::strtoul(n.id.c_str(), nullptr, 10));
    auto it = meta.find(id);
    if (it != meta.end()) {
      n.prefab_id = it->second.prefab_id;
      n.script_path = it->second.script_path;
    }
  }
}

void RestoreMeta(const game_kit::SceneDocument& stored, const game_kit::SceneDocument& captured,
                 std::unordered_map<engine::scene::NodeId, NodeMeta>* meta) {
  if (!meta) {
    return;
  }
  meta->clear();
  const std::size_t n = std::min(stored.nodes.size(), captured.nodes.size());
  for (std::size_t i = 0; i < n; ++i) {
    const auto id =
        static_cast<engine::scene::NodeId>(std::strtoul(captured.nodes[i].id.c_str(), nullptr, 10));
    (*meta)[id] = NodeMeta{stored.nodes[i].prefab_id, stored.nodes[i].script_path};
  }
}

void BindPlayScripts(game_kit::ScriptComponentWorld& scripts, engine::scene::World& world,
                     game_kit::GameRuntime& rt,
                     const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  scripts.Clear();
  scripts.AttachHost(&world, &rt);
  std::vector<engine::scene::NodeId> nodes;
  AllNodes(world, &nodes);
  for (auto id : nodes) {
    auto it = meta.find(id);
    if (it == meta.end() || it->second.script_path.empty()) {
      continue;
    }
    const auto cid = scripts.Attach(id, it->second.script_path);
    if (auto* c = scripts.Get(cid)) {
      (void)scripts.LoadFromDisk(*c);
    }
  }
}

void MovePlayerOnGround(engine::scene::World& world, engine::scene::NodeId player, float yaw,
                        const engine::Vec3& wish, float speed, float dt) {
  if (!world.valid(player) || dt <= 0.f) {
    return;
  }
  const engine::Vec3 fwd =
      engine::Quat::FromEulerYxz(yaw, 0.f, 0.f).Rotate(engine::Vec3{0.f, 0.f, -1.f});
  const engine::Vec3 right =
      engine::Quat::FromEulerYxz(yaw, 0.f, 0.f).Rotate(engine::Vec3{1.f, 0.f, 0.f});
  engine::Vec3 delta = fwd * wish.z + right * wish.x;
  if (delta.length_squared() > 1e-8f) {
    delta = engine::Normalize(delta) * (speed * dt);
  }
  auto t = world.local_transform(player);
  t.position += delta;
  world.set_local_transform(player, t);
}

void FollowPlayerCamera(engine::render::Camera* cam, const engine::Vec3& player_pos) {
  if (!cam) {
    return;
  }
  const engine::Vec3 eye = player_pos + engine::Vec3{0.f, 1.6f, 0.f};
  const engine::Vec3 fwd =
      engine::Quat::FromEulerYxz(cam->yaw, cam->pitch, 0.f).Rotate(engine::Vec3{0.f, 0.f, -1.f});
  cam->position = eye - fwd * 5.f;
}

}  // namespace editor
