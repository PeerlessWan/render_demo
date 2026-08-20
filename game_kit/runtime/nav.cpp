#include "game_kit/nav.h"

#include "game_kit/entity.h"

#include "engine/physics/i_physics_world.h"
#include "engine/scene/world.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
#include "DetourCrowd.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourStatus.h"
#include "Recast.h"
#endif

namespace game_kit {
namespace {

bool Overlaps(engine::Vec3 p, engine::Vec3 half, engine::Vec3 q) {
  return std::abs(p.x - q.x) <= half.x && std::abs(p.y - q.y) <= half.y &&
         std::abs(p.z - q.z) <= half.z;
}

engine::Vec3 PaddedHalf(engine::Vec3 h) {
  h.x = std::max(h.x, 0.25f);
  h.y = std::max(h.y, 0.25f);
  h.z = std::max(h.z, 0.25f);
  return h;
}

void PushBox(std::vector<float>* verts, std::vector<int>* tris, engine::Vec3 c, engine::Vec3 h) {
  h = PaddedHalf(h);
  const int base = static_cast<int>(verts->size() / 3);
  const float xs[2] = {c.x - h.x, c.x + h.x};
  const float ys[2] = {c.y - h.y, c.y + h.y};
  const float zs[2] = {c.z - h.z, c.z + h.z};
  for (int y = 0; y < 2; ++y) {
    for (int z = 0; z < 2; ++z) {
      for (int x = 0; x < 2; ++x) {
        verts->push_back(xs[x]);
        verts->push_back(ys[y]);
        verts->push_back(zs[z]);
      }
    }
  }
  const int faces[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1},
                            {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
  for (auto f : faces) {
    tris->push_back(base + f[0]);
    tris->push_back(base + f[1]);
    tris->push_back(base + f[2]);
  }
}

void VisitWorld(engine::scene::World& world, engine::scene::NodeId id, const engine::Mat4& parent,
                std::vector<NavObstacle>* boxes) {
  const auto local = world.local_transform(id);
  const engine::Mat4 m = parent * engine::Mat4::TRS(local.position, local.rotation, local.scale);
  if (const auto* mesh = world.mesh(id)) {
    engine::Vec3 mn{1e9f, 1e9f, 1e9f};
    engine::Vec3 mx{-1e9f, -1e9f, -1e9f};
    const auto b = mesh->local_bounds;
    const engine::Vec3 corners[8] = {
        {b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z}, {b.min.x, b.max.y, b.min.z},
        {b.max.x, b.max.y, b.min.z}, {b.min.x, b.min.y, b.max.z}, {b.max.x, b.min.y, b.max.z},
        {b.min.x, b.max.y, b.max.z}, {b.max.x, b.max.y, b.max.z},
    };
    for (const auto& c : corners) {
      const auto p = m.TransformPoint(c);
      mn.x = std::min(mn.x, p.x);
      mn.y = std::min(mn.y, p.y);
      mn.z = std::min(mn.z, p.z);
      mx.x = std::max(mx.x, p.x);
      mx.y = std::max(mx.y, p.y);
      mx.z = std::max(mx.z, p.z);
    }
    boxes->push_back(NavObstacle{(mn + mx) * 0.5f, PaddedHalf((mx - mn) * 0.5f)});
  }
  if (const auto* col = world.collider(id)) {
    const auto p = m.TransformPoint({});
    boxes->push_back(NavObstacle{p, PaddedHalf({col->hx, col->hy, col->hz})});
  }
  for (auto child : world.children(id)) {
    VisitWorld(world, child, m, boxes);
  }
}

}  // namespace

struct NavMeshImpl {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  dtNavMesh* mesh = nullptr;
  dtNavMeshQuery* query = nullptr;
  dtCrowd* crowd = nullptr;
  ~NavMeshImpl() {
    if (crowd) {
      dtFreeCrowd(crowd);
    }
    if (query) {
      dtFreeNavMeshQuery(query);
    }
    if (mesh) {
      dtFreeNavMesh(mesh);
    }
  }
#endif
};

NavWorld::NavWorld() = default;
NavWorld::~NavWorld() = default;
NavWorld::NavWorld(NavWorld&&) noexcept = default;
NavWorld& NavWorld::operator=(NavWorld&&) noexcept = default;

void NavWorld::AddObstacle(engine::Vec3 pos, engine::Vec3 half) {
  obstacles_.push_back(NavObstacle{pos, half});
}

void NavWorld::Clear() {
  obstacles_.clear();
  paths_.clear();
  agents_.clear();
  mesh_.reset();
}

engine::Vec3 NavWorld::Steer(engine::Vec3 from, engine::Vec3 goal, float speed, float dt) const {
  engine::Vec3 delta = goal - from;
  delta.y = 0.f;
  if (delta.length_squared() <= 0.0001f) {
    return from;
  }
  engine::Vec3 next = from + engine::Normalize(delta) * (speed * dt);
  for (const auto& o : obstacles_) {
    if (Overlaps(o.position, o.half_extents, next)) {
      engine::Vec3 slide = next;
      slide.x = from.x;
      if (!Overlaps(o.position, o.half_extents, slide)) {
        return slide;
      }
      slide = next;
      slide.z = from.z;
      if (!Overlaps(o.position, o.half_extents, slide)) {
        return slide;
      }
      return from;
    }
  }
  return next;
}

void NavWorld::SetSense(std::string hunter, std::string prey, float range) {
  hunter_ = std::move(hunter);
  prey_ = std::move(prey);
  chase_range_ = range;
}

void NavWorld::TickConfiguredSense(EntityWorld& entities, engine::scene::World* world) {
  if (hunter_.empty() || prey_.empty() || chase_range_ <= 0.f) {
    return;
  }
  TickSense(entities, world, hunter_, prey_, chase_range_);
}

void NavWorld::SetPath(std::string entity, std::vector<engine::Vec3> points, float speed) {
  for (auto& p : paths_) {
    if (p.entity == entity) {
      p.points = std::move(points);
      p.index = 0;
      p.speed = speed;
      return;
    }
  }
  Follow f;
  f.entity = std::move(entity);
  f.points = std::move(points);
  f.speed = speed;
  paths_.push_back(std::move(f));
}

engine::Vec3 NavWorld::FollowStep(engine::Vec3 from, engine::Vec3 goal, float speed, float dt) const {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (has_navmesh()) {
    engine::Vec3 delta = goal - from;
    if (delta.length_squared() > 0.0001f) {
      const engine::Vec3 next = from + engine::Normalize(delta) * (speed * dt);
      const float ext[3] = {2.f, 4.f, 2.f};
      const float s[3] = {from.x, from.y, from.z};
      const float n[3] = {next.x, next.y, next.z};
      dtQueryFilter filter;
      dtPolyRef startRef = 0;
      float sn[3]{};
      mesh_->query->findNearestPoly(s, ext, &filter, &startRef, sn);
      if (startRef) {
        float result[3]{};
        dtPolyRef visited[16];
        int nvisited = 0;
        const auto st =
            mesh_->query->moveAlongSurface(startRef, sn, n, &filter, result, visited, &nvisited, 16);
        if (dtStatusSucceed(st)) {
          return {result[0], result[1], result[2]};
        }
      }
    }
  }
#endif
  return Steer(from, goal, speed, dt);
}

void NavWorld::TickFollow(EntityWorld& entities, engine::scene::World* world, float dt) {
  if (!world || dt <= 0.f) {
    return;
  }
  for (auto& p : paths_) {
    if (p.index >= p.points.size()) {
      continue;
    }
    Entity* e = entities.FindByName(p.entity);
    if (!e || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
      continue;
    }
    auto t = world->local_transform(e->node);
    const auto goal = p.points[p.index];
    t.position = FollowStep(t.position, goal, p.speed, dt);
    world->set_local_transform(e->node, t);
    engine::Vec3 d = goal - t.position;
    d.y = 0.f;
    if (d.length_squared() <= p.arrive * p.arrive) {
      ++p.index;
    }
  }
}

bool NavWorld::PathFinished(std::string_view entity) const {
  for (const auto& p : paths_) {
    if (p.entity == entity) {
      return p.index >= p.points.size();
    }
  }
  return true;
}

void NavWorld::TickSense(EntityWorld& entities, engine::scene::World* world, std::string_view hunter,
                         std::string_view prey, float chase_range) {
  Entity* h = entities.FindByName(hunter);
  Entity* p = entities.FindByName(prey);
  if (!h || !p) {
    return;
  }
  if (!world || h->node == engine::scene::kInvalidNode || p->node == engine::scene::kInvalidNode ||
      !world->valid(h->node) || !world->valid(p->node)) {
    return;
  }
  const auto a = world->local_transform(h->node).position;
  const auto b = world->local_transform(p->node).position;
  const float dist2 = (a - b).length_squared();
  const float r2 = chase_range * chase_range;
  if (dist2 <= r2) {
    h->ai.Set(AiState::Chase);
  } else if (h->ai.state == AiState::Chase) {
    h->ai.Set(AiState::Idle);
  }
}

bool NavWorld::has_navmesh() const {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  return mesh_ && mesh_->mesh && mesh_->query;
#else
  return false;
#endif
}

void NavWorld::ResetCrowd() {
  agents_.clear();
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (!mesh_) {
    return;
  }
  if (mesh_->crowd) {
    dtFreeCrowd(mesh_->crowd);
    mesh_->crowd = nullptr;
  }
  if (mesh_->mesh) {
    mesh_->crowd = dtAllocCrowd();
    if (mesh_->crowd) {
      (void)mesh_->crowd->init(16, 0.6f, mesh_->mesh);
    }
  }
#endif
}

bool NavWorld::BakeFromObstacles() {
  std::vector<NavObstacle> boxes = obstacles_;
  boxes.insert(boxes.begin(), NavObstacle{{0.f, -0.25f, 0.f}, {20.f, 0.25f, 20.f}});
  return BakeBoxes(boxes);
}

bool NavWorld::BakeFromPhysics(const engine::physics::IPhysicsWorld& phys) {
  std::vector<NavObstacle> boxes;
  const int n = phys.body_count();
  for (int i = 0; i < n; ++i) {
    boxes.push_back(NavObstacle{phys.body_position(i), phys.body_half_extents(i)});
  }
  if (boxes.empty()) {
    return BakeFromObstacles();
  }
  return BakeBoxes(boxes);
}

bool NavWorld::BakeFromWorld(engine::scene::World& world) {
  std::vector<NavObstacle> boxes;
  for (auto root : world.roots()) {
    VisitWorld(world, root, engine::Mat4::Identity(), &boxes);
  }
  boxes.insert(boxes.end(), obstacles_.begin(), obstacles_.end());
  bool wide_floor = false;
  for (const auto& b : boxes) {
    if (b.half_extents.x >= 2.f && b.half_extents.z >= 2.f) {
      wide_floor = true;
      break;
    }
  }
  if (!wide_floor) {
    boxes.insert(boxes.begin(), NavObstacle{{0.f, -0.25f, 0.f}, {20.f, 0.25f, 20.f}});
  }
  if (boxes.empty()) {
    return BakeFromObstacles();
  }
  return BakeBoxes(boxes);
}

bool NavWorld::BakeBoxes(const std::vector<NavObstacle>& boxes) {
  mesh_.reset();
  agents_.clear();
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  std::vector<float> verts;
  std::vector<int> tris;
  for (const auto& o : boxes) {
    PushBox(&verts, &tris, o.position, o.half_extents);
  }
  if (verts.empty() || tris.empty()) {
    return false;
  }

  rcConfig cfg{};
  cfg.cs = 0.3f;
  cfg.ch = 0.2f;
  cfg.walkableSlopeAngle = 45.f;
  cfg.walkableHeight = static_cast<int>(std::ceil(2.f / cfg.ch));
  cfg.walkableClimb = static_cast<int>(std::floor(0.4f / cfg.ch));
  cfg.walkableRadius = static_cast<int>(std::ceil(0.4f / cfg.cs));
  cfg.maxEdgeLen = static_cast<int>(12.f / cfg.cs);
  cfg.maxSimplificationError = 1.3f;
  cfg.minRegionArea = 8;
  cfg.mergeRegionArea = 20;
  cfg.maxVertsPerPoly = 6;
  cfg.detailSampleDist = 6.f;
  cfg.detailSampleMaxError = 1.f;

  rcCalcBounds(verts.data(), static_cast<int>(verts.size() / 3), cfg.bmin, cfg.bmax);
  rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

  rcContext ctx(false);
  rcHeightfield* hf = rcAllocHeightfield();
  rcCompactHeightfield* chf = nullptr;
  rcContourSet* cset = nullptr;
  rcPolyMesh* pmesh = nullptr;
  rcPolyMeshDetail* dmesh = nullptr;
  auto cleanup = [&] {
    rcFreeHeightField(hf);
    rcFreeCompactHeightfield(chf);
    rcFreeContourSet(cset);
    rcFreePolyMesh(pmesh);
    rcFreePolyMeshDetail(dmesh);
  };
  if (!hf || !rcCreateHeightfield(&ctx, *hf, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) {
    cleanup();
    return false;
  }
  std::vector<unsigned char> areas(static_cast<std::size_t>(tris.size() / 3), 0);
  rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts.data(), static_cast<int>(verts.size() / 3),
                          tris.data(), static_cast<int>(tris.size() / 3), areas.data());
  if (!rcRasterizeTriangles(&ctx, verts.data(), static_cast<int>(verts.size() / 3), tris.data(),
                            areas.data(), static_cast<int>(tris.size() / 3), *hf, cfg.walkableClimb)) {
    cleanup();
    return false;
  }
  rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *hf);
  rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf);
  rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *hf);
  chf = rcAllocCompactHeightfield();
  if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *hf, *chf)) {
    cleanup();
    return false;
  }
  rcFreeHeightField(hf);
  hf = nullptr;
  if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf)) {
    cleanup();
    return false;
  }
  if (!rcBuildDistanceField(&ctx, *chf) ||
      !rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea)) {
    cleanup();
    return false;
  }
  cset = rcAllocContourSet();
  if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset)) {
    cleanup();
    return false;
  }
  pmesh = rcAllocPolyMesh();
  if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh)) {
    cleanup();
    return false;
  }
  dmesh = rcAllocPolyMeshDetail();
  if (!dmesh ||
      !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh)) {
    cleanup();
    return false;
  }
  for (int i = 0; i < pmesh->npolys; ++i) {
    if (pmesh->areas[i] == RC_WALKABLE_AREA) {
      pmesh->flags[i] = 1;
    }
  }

  dtNavMeshCreateParams params{};
  params.verts = pmesh->verts;
  params.vertCount = pmesh->nverts;
  params.polys = pmesh->polys;
  params.polyAreas = pmesh->areas;
  params.polyFlags = pmesh->flags;
  params.polyCount = pmesh->npolys;
  params.nvp = pmesh->nvp;
  params.detailMeshes = dmesh->meshes;
  params.detailVerts = dmesh->verts;
  params.detailVertsCount = dmesh->nverts;
  params.detailTris = dmesh->tris;
  params.detailTriCount = dmesh->ntris;
  params.walkableHeight = 2.f;
  params.walkableRadius = 0.4f;
  params.walkableClimb = 0.4f;
  params.cs = cfg.cs;
  params.ch = cfg.ch;
  params.buildBvTree = true;
  rcVcopy(params.bmin, pmesh->bmin);
  rcVcopy(params.bmax, pmesh->bmax);

  unsigned char* navData = nullptr;
  int navDataSize = 0;
  if (!dtCreateNavMeshData(&params, &navData, &navDataSize) || !navData) {
    cleanup();
    return false;
  }
  auto impl = std::make_unique<NavMeshImpl>();
  impl->mesh = dtAllocNavMesh();
  impl->query = dtAllocNavMeshQuery();
  if (!impl->mesh || impl->mesh->init(navData, navDataSize, DT_TILE_FREE_DATA) != DT_SUCCESS ||
      !impl->query || impl->query->init(impl->mesh, 2048) != DT_SUCCESS) {
    dtFree(navData);
    cleanup();
    return false;
  }
  cleanup();
  mesh_ = std::move(impl);
  ResetCrowd();
  return true;
#else
  (void)boxes;
  return false;
#endif
}

std::vector<engine::Vec3> NavWorld::FindPath(engine::Vec3 from, engine::Vec3 to) const {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (!has_navmesh()) {
    return {from, to};
  }
  const float ext[3] = {2.f, 4.f, 2.f};
  const float s[3] = {from.x, from.y, from.z};
  const float e[3] = {to.x, to.y, to.z};
  dtQueryFilter filter;
  dtPolyRef startRef = 0;
  dtPolyRef endRef = 0;
  float sn[3]{};
  float en[3]{};
  mesh_->query->findNearestPoly(s, ext, &filter, &startRef, sn);
  mesh_->query->findNearestPoly(e, ext, &filter, &endRef, en);
  if (!startRef || !endRef) {
    return {from, to};
  }
  dtPolyRef path[256];
  int npath = 0;
  mesh_->query->findPath(startRef, endRef, sn, en, &filter, path, &npath, 256);
  if (npath <= 0) {
    return {from, to};
  }
  float straight[256 * 3];
  unsigned char flags[256];
  dtPolyRef polys[256];
  int nstraight = 0;
  mesh_->query->findStraightPath(sn, en, path, npath, straight, flags, polys, &nstraight, 256);
  std::vector<engine::Vec3> out;
  out.reserve(static_cast<std::size_t>(nstraight));
  for (int i = 0; i < nstraight; ++i) {
    out.push_back({straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]});
  }
  if (out.empty()) {
    out.push_back(from);
    out.push_back(to);
  }
  return out;
#else
  return {from, to};
#endif
}

int NavWorld::AddAgent(std::string name, engine::Vec3 pos, float speed) {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (!has_navmesh()) {
    return -1;
  }
  if (!mesh_->crowd) {
    ResetCrowd();
  }
  if (!mesh_->crowd) {
    return -1;
  }
  dtCrowdAgentParams ap{};
  ap.radius = 0.4f;
  ap.height = 1.8f;
  ap.maxAcceleration = 8.f;
  ap.maxSpeed = speed;
  ap.collisionQueryRange = ap.radius * 8.f;
  ap.pathOptimizationRange = ap.radius * 30.f;
  ap.separationWeight = 2.f;
  ap.updateFlags = DT_CROWD_SEPARATION | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_ANTICIPATE_TURNS;
  const float p[3] = {pos.x, pos.y, pos.z};
  const int idx = mesh_->crowd->addAgent(p, &ap);
  if (idx < 0) {
    return -1;
  }
  agents_.push_back({std::move(name), idx});
  return idx;
#else
  (void)name;
  (void)pos;
  (void)speed;
  return -1;
#endif
}

void NavWorld::SetAgentTarget(std::string_view name, engine::Vec3 pos) {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (!has_navmesh() || !mesh_->crowd) {
    return;
  }
  int idx = -1;
  for (const auto& a : agents_) {
    if (a.first == name) {
      idx = a.second;
      break;
    }
  }
  if (idx < 0) {
    return;
  }
  const float ext[3] = {2.f, 4.f, 2.f};
  const float e[3] = {pos.x, pos.y, pos.z};
  dtQueryFilter filter;
  dtPolyRef ref = 0;
  float nearest[3]{};
  mesh_->query->findNearestPoly(e, ext, &filter, &ref, nearest);
  if (ref) {
    (void)mesh_->crowd->requestMoveTarget(idx, ref, nearest);
  }
#else
  (void)name;
  (void)pos;
#endif
}

void NavWorld::TickCrowd(float dt) {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (mesh_ && mesh_->crowd && dt > 0.f) {
    mesh_->crowd->update(dt, nullptr);
  }
#else
  (void)dt;
#endif
}

engine::Vec3 NavWorld::AgentPosition(std::string_view name) {
#if defined(GAME_KIT_WITH_RECAST) && GAME_KIT_WITH_RECAST
  if (!mesh_ || !mesh_->crowd) {
    return {};
  }
  for (const auto& a : agents_) {
    if (a.first == name) {
      const auto* ag = mesh_->crowd->getAgent(a.second);
      if (ag && ag->active) {
        return {ag->npos[0], ag->npos[1], ag->npos[2]};
      }
    }
  }
#else
  (void)name;
#endif
  return {};
}

void NavWorld::SyncAgents(EntityWorld& entities, engine::scene::World* world) {
  if (!world) {
    return;
  }
  for (const auto& a : agents_) {
    Entity* e = entities.FindByName(a.first);
    if (!e || e->node == engine::scene::kInvalidNode || !world->valid(e->node)) {
      continue;
    }
    auto t = world->local_transform(e->node);
    t.position = AgentPosition(a.first);
    world->set_local_transform(e->node, t);
  }
}

}  // namespace game_kit
