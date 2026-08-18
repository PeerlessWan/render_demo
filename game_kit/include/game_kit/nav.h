#pragma once

#include "engine/core/math.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace engine::scene {
class World;
}
namespace engine::physics {
class IPhysicsWorld;
}

namespace game_kit {

class EntityWorld;

struct NavObstacle {
  engine::Vec3 position{};
  engine::Vec3 half_extents{0.5f, 0.5f, 0.5f};
};

struct NavMeshImpl;

class NavWorld {
 public:
  NavWorld();
  ~NavWorld();
  NavWorld(NavWorld&&) noexcept;
  NavWorld& operator=(NavWorld&&) noexcept;
  NavWorld(const NavWorld&) = delete;
  NavWorld& operator=(const NavWorld&) = delete;

  void AddObstacle(engine::Vec3 pos, engine::Vec3 half);
  void Clear();
  engine::Vec3 Steer(engine::Vec3 from, engine::Vec3 goal, float speed, float dt) const;
  [[nodiscard]] const std::vector<NavObstacle>& obstacles() const { return obstacles_; }

  void TickSense(EntityWorld& entities, engine::scene::World* world, std::string_view hunter,
                 std::string_view prey, float chase_range);

  void SetSense(std::string hunter, std::string prey, float range);
  void TickConfiguredSense(EntityWorld& entities, engine::scene::World* world);

  void SetPath(std::string entity, std::vector<engine::Vec3> points, float speed = 4.f);
  void TickFollow(EntityWorld& entities, engine::scene::World* world, float dt);
  [[nodiscard]] bool PathFinished(std::string_view entity) const;

  bool BakeFromObstacles();
  bool BakeFromPhysics(const engine::physics::IPhysicsWorld& phys);
  bool BakeFromWorld(engine::scene::World& world);
  [[nodiscard]] std::vector<engine::Vec3> FindPath(engine::Vec3 from, engine::Vec3 to) const;
  [[nodiscard]] bool has_navmesh() const;

  int AddAgent(std::string name, engine::Vec3 pos, float speed = 4.f);
  void SetAgentTarget(std::string_view name, engine::Vec3 pos);
  void TickCrowd(float dt);
  [[nodiscard]] engine::Vec3 AgentPosition(std::string_view name);
  void SyncAgents(EntityWorld& entities, engine::scene::World* world);

 private:
  bool BakeBoxes(const std::vector<NavObstacle>& boxes);
  engine::Vec3 FollowStep(engine::Vec3 from, engine::Vec3 goal, float speed, float dt) const;
  void ResetCrowd();

  std::vector<NavObstacle> obstacles_;
  std::string hunter_;
  std::string prey_;
  float chase_range_ = 0.f;
  std::unique_ptr<NavMeshImpl> mesh_;
  std::vector<std::pair<std::string, int>> agents_;

  struct Follow {
    std::string entity;
    std::vector<engine::Vec3> points;
    std::size_t index = 0;
    float speed = 4.f;
    float arrive = 0.25f;
  };
  std::vector<Follow> paths_;
};

}  // namespace game_kit
