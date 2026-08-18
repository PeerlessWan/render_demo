#include "scene_io.h"

#include <utility>

namespace editor {

engine::Status SaveScene(const engine::scene::World& world, const std::filesystem::path& path) {
  return game_kit::SaveSceneDocument(game_kit::CaptureWorld(world), path);
}

engine::Status SaveScene(const engine::scene::World& world, const std::filesystem::path& path,
                         const std::unordered_map<engine::scene::NodeId, NodeMeta>& meta) {
  auto doc = game_kit::CaptureWorld(world);
  StampMeta(&doc, meta);
  return game_kit::SaveSceneDocument(doc, path);
}

engine::Status LoadScene(engine::scene::World& world, const std::filesystem::path& path) {
  auto doc = game_kit::LoadSceneDocument(path);
  if (!doc) {
    return doc.status();
  }
  engine::scene::World next;
  if (auto st = game_kit::ApplyWorld(next, doc.value()); !st) {
    return st;
  }
  world = std::move(next);
  return engine::Status::Ok();
}

}  // namespace editor
