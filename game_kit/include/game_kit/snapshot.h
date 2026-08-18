#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace engine::scene {
class World;
}

namespace game_kit {

class EntityWorld;
class GameRuntime;
class SaveSlots;

inline constexpr int kSnapshotFormatCurrent = 1;

struct EntitySnap {
  std::string name;
  engine::Vec3 position{};
  engine::Quat rotation{};
  engine::Vec3 scale{1.f, 1.f, 1.f};
  std::string ai = "Idle";
  bool active = true;
  std::vector<std::string> tags;
  std::string script_path;
  std::string persist;
};

struct WorldSnapshot {
  int format_version = kSnapshotFormatCurrent;
  std::string level;
  std::uint32_t tick = 0;
  std::vector<EntitySnap> entities;
};

WorldSnapshot CaptureSnapshot(const EntityWorld& entities, const engine::scene::World* world,
                              std::string_view level);
WorldSnapshot CaptureSnapshot(const GameRuntime& rt, const engine::scene::World* world);
engine::Status ApplySnapshot(GameRuntime& rt, engine::scene::World* world, const WorldSnapshot& snap);

std::string SnapshotToJson(const WorldSnapshot& snap);
engine::Result<WorldSnapshot> SnapshotFromJson(std::string_view json);

engine::Status SaveSnapshot(SaveSlots& saves, int slot, const WorldSnapshot& snap);
engine::Result<WorldSnapshot> LoadSnapshot(const SaveSlots& saves, int slot);

class LoopbackReplicator {
 public:
  void Push(WorldSnapshot snap) {
    snap.tick = ++tick_;
    last_ = snap;
    has_last_ = true;
    pending_ = std::move(snap);
    has_ = true;
  }
  [[nodiscard]] bool has() const { return has_; }
  WorldSnapshot Pull() {
    has_ = false;
    return pending_;
  }

  void PushDiff(const WorldSnapshot& now);
  [[nodiscard]] bool has_diff() const { return has_ && !pending_.entities.empty(); }
  [[nodiscard]] std::uint32_t tick() const { return tick_; }

 private:
  WorldSnapshot pending_;
  WorldSnapshot last_;
  bool has_ = false;
  bool has_last_ = false;
  std::uint32_t tick_ = 0;
};

// In-process authority → replica interpolation (not a net server).
class ReplicationSession {
 public:
  void ServerCapture(const GameRuntime& rt, const engine::scene::World* world);
  void ClientApply(GameRuntime& rt, engine::scene::World* world, float dt);
  [[nodiscard]] std::uint32_t tick() const { return tick_; }
  [[nodiscard]] bool has_frame() const { return has_; }

 private:
  LoopbackReplicator transport_;
  WorldSnapshot target_;
  std::uint32_t tick_ = 0;
  bool has_ = false;
};

}  // namespace game_kit
