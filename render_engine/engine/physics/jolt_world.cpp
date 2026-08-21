#include "engine/physics/i_physics_world.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

JPH_SUPPRESS_WARNINGS

namespace engine::physics {
namespace {

void TraceImpl(const char* fmt, ...) {
  char buffer[1024];
  va_list list;
  va_start(list, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, list);
  va_end(list);
  std::fputs(buffer, stderr);
  std::fputc('\n', stderr);
}

namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kNumLayers = 2;
}  // namespace Layers

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    switch (a) {
      case Layers::kNonMoving:
        return b == Layers::kMoving;
      case Layers::kMoving:
        return true;
      default:
        return false;
    }
  }
};

namespace BroadPhaseLayers {
constexpr JPH::BroadPhaseLayer kNonMoving(0);
constexpr JPH::BroadPhaseLayer kMoving(1);
constexpr JPH::uint kNumLayers = 2;
}  // namespace BroadPhaseLayers

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
 public:
  BPLayerInterfaceImpl() {
    object_to_bp_[Layers::kNonMoving] = BroadPhaseLayers::kNonMoving;
    object_to_bp_[Layers::kMoving] = BroadPhaseLayers::kMoving;
  }

  JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::kNumLayers; }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return object_to_bp_[layer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    switch ((JPH::BroadPhaseLayer::Type)layer) {
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::kNonMoving:
        return "NON_MOVING";
      case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::kMoving:
        return "MOVING";
      default:
        return "INVALID";
    }
  }
#endif

 private:
  JPH::BroadPhaseLayer object_to_bp_[Layers::kNumLayers];
};

class ObjectVsBroadPhaseLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
    switch (layer) {
      case Layers::kNonMoving:
        return bp == BroadPhaseLayers::kMoving;
      case Layers::kMoving:
        return true;
      default:
        return false;
    }
  }
};

void EnsureJoltTypesRegistered() {
  static std::once_flag once;
  std::call_once(once, [] {
    JPH::RegisterDefaultAllocator();
    JPH::Trace = TraceImpl;
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
  });
}

class ContactCollector final : public JPH::ContactListener {
 public:
  void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                      JPH::ContactSettings&) override {
    Add(a, b, true, manifold);
  }
  void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold,
                          JPH::ContactSettings&) override {
    Add(a, b, false, manifold);
  }
  void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
    (void)pair;
    // Area exit tracked via Step pair diff when sensors used.
  }

  std::vector<ContactPair> Take() {
    std::vector<ContactPair> out = std::move(pairs_);
    pairs_.clear();
    seen_.clear();
    return out;
  }

  std::vector<IPhysicsWorld::AreaEvent3D> TakeAreaEvents() {
    auto out = std::move(area_events_);
    area_events_.clear();
    return out;
  }

  void NoteSensor(int body_index, bool is_sensor) {
    if (body_index < 0) {
      return;
    }
    if (static_cast<int>(sensor_flags_.size()) <= body_index) {
      sensor_flags_.resize(static_cast<std::size_t>(body_index) + 1, false);
    }
    sensor_flags_[static_cast<std::size_t>(body_index)] = is_sensor;
  }

 private:
  static int BodyIndex(const JPH::Body& b) {
    const JPH::uint64 user = b.GetUserData();
    if (user == static_cast<JPH::uint64>(-1) || user == static_cast<JPH::uint64>(-2)) {
      return -1;
    }
    return static_cast<int>(user);
  }

  void Add(const JPH::Body& a, const JPH::Body& b, bool added, const JPH::ContactManifold&) {
    int ia = BodyIndex(a);
    int ib = BodyIndex(b);
    if (ia < 0 || ib < 0) {
      return;
    }
    const bool a_sensor =
        ia < static_cast<int>(sensor_flags_.size()) && sensor_flags_[static_cast<std::size_t>(ia)];
    const bool b_sensor =
        ib < static_cast<int>(sensor_flags_.size()) && sensor_flags_[static_cast<std::size_t>(ib)];
    if (added && (a_sensor || b_sensor)) {
      if (a_sensor) {
        area_events_.push_back({ia, ib, true});
      }
      if (b_sensor) {
        area_events_.push_back({ib, ia, true});
      }
    }
    if (ia > ib) {
      std::swap(ia, ib);
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ia)) << 32) |
                              static_cast<std::uint32_t>(ib);
    for (auto k : seen_) {
      if (k == key) {
        return;
      }
    }
    seen_.push_back(key);
    pairs_.push_back(ContactPair{ia, ib});
  }

  std::vector<ContactPair> pairs_;
  std::vector<std::uint64_t> seen_;
  std::vector<IPhysicsWorld::AreaEvent3D> area_events_;
  std::vector<bool> sensor_flags_;
};

class JoltWorld final : public IPhysicsWorld {
 public:
  JoltWorld() {
    EnsureJoltTypesRegistered();
    temp_allocator_ = std::make_unique<JPH::TempAllocatorImpl>(8 * 1024 * 1024);
    job_system_ = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);

    constexpr JPH::uint kMaxBodies = 2048;
    constexpr JPH::uint kMaxBodyPairs = 4096;
    constexpr JPH::uint kMaxContactConstraints = 4096;
    physics_.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broadphase_layers_,
                  object_vs_broadphase_, object_vs_object_);
    contacts_ = std::make_unique<ContactCollector>();
    physics_.SetContactListener(contacts_.get());

    JPH::BoxShapeSettings floor_shape_settings(JPH::Vec3(200.f, 1.f, 200.f));
    floor_shape_settings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult floor_shape_result = floor_shape_settings.Create();
    JPH::ShapeRefC floor_shape = floor_shape_result.Get();
    JPH::BodyCreationSettings floor_settings(floor_shape, JPH::RVec3(0, -1, 0),
                                             JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                             Layers::kNonMoving);
    floor_settings.mUserData = static_cast<JPH::uint64>(-1);
    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    floor_id_ = bodies.CreateAndAddBody(floor_settings, JPH::EActivation::DontActivate);
    physics_.OptimizeBroadPhase();
  }

  ~JoltWorld() override {
    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    for (auto* c : joint_ptrs_) {
      if (c) {
        physics_.RemoveConstraint(c);
      }
    }
    joint_ptrs_.clear();
    for (JPH::BodyID id : soft_body_ids_) {
      if (!id.IsInvalid()) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
      }
    }
    for (JPH::BodyID id : body_ids_) {
      if (!id.IsInvalid()) {
        bodies.RemoveBody(id);
        bodies.DestroyBody(id);
      }
    }
    if (!floor_id_.IsInvalid()) {
      bodies.RemoveBody(floor_id_);
      bodies.DestroyBody(floor_id_);
    }
  }

  int CreateBox(const RigidBodyDesc& desc) override {
    const JPH::Vec3 half(desc.half_extents.x, desc.half_extents.y, desc.half_extents.z);
    JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(half);

    const bool is_static = desc.mass <= 0.f && !desc.is_trigger;
    JPH::EMotionType motion = JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = Layers::kMoving;
    if (is_static) {
      motion = JPH::EMotionType::Static;
      layer = Layers::kNonMoving;
    } else if (desc.is_trigger) {
      motion = JPH::EMotionType::Kinematic;
      layer = Layers::kMoving;
    }

    JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
                                       JPH::Quat::sIdentity(), motion, layer);
    settings.mIsSensor = desc.is_trigger;
    if (motion == JPH::EMotionType::Dynamic && desc.mass > 0.f) {
      settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    const int index = static_cast<int>(body_ids_.size());
    settings.mUserData = static_cast<JPH::uint64>(index);

    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    const JPH::BodyID id = bodies.CreateAndAddBody(
        settings, motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                     : JPH::EActivation::Activate);
    if (id.IsInvalid()) {
      return -1;
    }
    body_ids_.push_back(id);
    half_extents_.push_back(desc.half_extents);
    body_is_trigger_.push_back(desc.is_trigger);
    body_layers_.push_back(desc.collision_layer);
    body_masks_.push_back(desc.collision_mask);
    char_on_floor_.push_back(false);
    char_floor_n_.push_back({0, 1, 0});
    if (contacts_) {
      contacts_->NoteSensor(index, desc.is_trigger);
    }
    return index;
  }

  int CreateCapsule(const CapsuleDesc& desc) override {
    const float radius = std::max(desc.radius, 0.05f);
    const float half_h = std::max(desc.half_height, 0.05f);
    JPH::RefConst<JPH::Shape> shape = new JPH::CapsuleShape(half_h, radius);

    const bool is_static = desc.mass <= 0.f;
    JPH::EMotionType motion =
        is_static ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
    JPH::ObjectLayer layer = Layers::kMoving;

    JPH::BodyCreationSettings settings(shape, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
                                       JPH::Quat::sIdentity(), motion, layer);
    if (motion == JPH::EMotionType::Dynamic && desc.mass > 0.f) {
      settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
      settings.mMassPropertiesOverride.mMass = desc.mass;
    }

    const int index = static_cast<int>(body_ids_.size());
    settings.mUserData = static_cast<JPH::uint64>(index);

    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    const JPH::BodyID id =
        bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
      return -1;
    }
    body_ids_.push_back(id);
    // AABB half-extents approx for queries / ground snap.
    half_extents_.push_back({radius, half_h + radius, radius});
    body_is_trigger_.push_back(false);
    body_layers_.push_back(1u);
    body_masks_.push_back(0xFFFFFFFFu);
    char_on_floor_.push_back(false);
    char_floor_n_.push_back({0, 1, 0});
    return index;
  }

  void Step(float dt) override {
    if (dt <= 0.f) {
      return;
    }
    int collision_steps = 1;
    if (dt > (1.f / 60.f) + 1e-4f) {
      collision_steps = static_cast<int>(dt / (1.f / 60.f)) + 1;
    }
    physics_.Update(dt, collision_steps, temp_allocator_.get(), job_system_.get());
  }

  RayHit Raycast(const Vec3& origin, const Vec3& dir_in, float max_dist) const override {
    RayHit out;
    const Vec3 dir = Normalize(dir_in);
    const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z),
                            JPH::Vec3(dir.x, dir.y, dir.z) * max_dist);
    JPH::RayCastResult hit;
    if (!physics_.GetNarrowPhaseQuery().CastRay(ray, hit)) {
      return out;
    }

    out.hit = true;
    out.distance = hit.mFraction * max_dist;
    const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
    out.point = {static_cast<float>(point.GetX()), static_cast<float>(point.GetY()),
                 static_cast<float>(point.GetZ())};
    out.normal = {0, 1, 0};
    out.body_id = -1;

    JPH::BodyLockRead lock(physics_.GetBodyLockInterface(), hit.mBodyID);
    if (lock.Succeeded()) {
      const JPH::Body& body = lock.GetBody();
      const JPH::Vec3 n = body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
      out.normal = {n.GetX(), n.GetY(), n.GetZ()};
      const JPH::uint64 user = body.GetUserData();
      if (user != static_cast<JPH::uint64>(-1) && user < body_ids_.size()) {
        out.body_id = static_cast<int>(user);
      }
    }
    return out;
  }

  Status MoveCharacter(int body_id, const Vec3& displacement) override {
    if (body_id < 0 || body_id >= static_cast<int>(body_ids_.size())) {
      return Status::Fail(ErrorCode::NotFound, "body not found");
    }
    const JPH::BodyID id = body_ids_[static_cast<std::size_t>(body_id)];
    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    if (bodies.GetMotionType(id) != JPH::EMotionType::Kinematic) {
      bodies.SetMotionType(id, JPH::EMotionType::Kinematic, JPH::EActivation::Activate);
      bodies.SetObjectLayer(id, Layers::kMoving);
    }

    const JPH::RVec3 cur = bodies.GetPosition(id);
    const Vec3 he = half_extents_[static_cast<std::size_t>(body_id)];
    JPH::Vec3 horiz(displacement.x, 0.f, displacement.z);
    JPH::RVec3 new_pos = cur;

    // Horizontal move via shape cast against world (ignore self).
    const float horiz_len = horiz.Length();
    if (horiz_len > 1e-5f) {
      JPH::RefConst<JPH::Shape> shape = bodies.GetShape(id);
      if (shape != nullptr) {
        const JPH::RMat44 start = JPH::RMat44::sTranslation(cur);
        const JPH::RShapeCast shape_cast(shape, JPH::Vec3::sOne(), start, horiz);
        JPH::ShapeCastSettings settings;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        JPH::IgnoreSingleBodyFilter body_filter(id);
        physics_.GetNarrowPhaseQuery().CastShape(
            shape_cast, settings, cur, collector, {}, {}, body_filter);
        float fraction = 1.f;
        if (collector.HadHit()) {
          fraction = std::max(0.f, collector.mHit.mFraction - 0.01f);
        }
        new_pos = cur + JPH::RVec3(horiz * fraction);
      } else {
        new_pos = cur + JPH::RVec3(horiz);
      }
    }

    new_pos = JPH::RVec3(new_pos.GetX(), cur.GetY() + displacement.y, new_pos.GetZ());

    // Ground snap: ray from character center downward.
    const float snap_dist = he.y * 2.f + 0.75f;
    const JPH::RVec3 ray_origin(new_pos.GetX(), new_pos.GetY() + 0.05f, new_pos.GetZ());
    const JPH::RRayCast ray(ray_origin, JPH::Vec3(0, -1, 0) * snap_dist);
    JPH::RayCastResult hit;
    JPH::IgnoreSingleBodyFilter body_filter(id);
    bool on_floor = false;
    Vec3 floor_n{0, 1, 0};
    if (physics_.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, body_filter)) {
      const JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
      const float ground_y = static_cast<float>(point.GetY());
      const float feet = static_cast<float>(new_pos.GetY()) - he.y;
      if (feet <= ground_y + 0.4f) {
        new_pos.SetY(ground_y + he.y);
        on_floor = true;
        JPH::BodyLockRead lock(physics_.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded()) {
          const JPH::Vec3 n =
              lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
          floor_n = {n.GetX(), n.GetY(), n.GetZ()};
        }
      }
    } else {
      const float floor_y = he.y;
      if (new_pos.GetY() < floor_y) {
        new_pos.SetY(floor_y);
        on_floor = true;
      }
    }

    bodies.SetPosition(id, new_pos, JPH::EActivation::Activate);
    if (body_id >= 0 && body_id < static_cast<int>(char_on_floor_.size())) {
      char_on_floor_[static_cast<std::size_t>(body_id)] = on_floor;
      char_floor_n_[static_cast<std::size_t>(body_id)] = floor_n;
    }
    return Status::Ok();
  }

  Status MoveCharacterEx(int body_id, const Vec3& displacement,
                         const CharacterMoveParams& params) override {
    if (body_id < 0 || body_id >= static_cast<int>(body_ids_.size())) {
      return Status::Fail(ErrorCode::NotFound, "body not found");
    }
    CharacterMoveParams p = params;
    if (p.max_step_height < 0.f) {
      p.max_step_height = 0.f;
    }
    const Vec3 he = half_extents_[static_cast<std::size_t>(body_id)];
    const JPH::BodyID id = body_ids_[static_cast<std::size_t>(body_id)];
    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    const JPH::RVec3 start = bodies.GetPosition(id);

    Status st = MoveCharacter(body_id, displacement);
    if (!st) {
      return st;
    }

    const JPH::RVec3 after = bodies.GetPosition(id);
    // Slope limit via ground normal.
    const float snap_dist = he.y * 2.f + 0.75f;
    const JPH::RVec3 ray_origin(after.GetX(), after.GetY() + 0.05f, after.GetZ());
    const JPH::RRayCast ray(ray_origin, JPH::Vec3(0, -1, 0) * snap_dist);
    JPH::RayCastResult hit;
    JPH::IgnoreSingleBodyFilter body_filter(id);
    if (physics_.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, body_filter)) {
      JPH::BodyLockRead lock(physics_.GetBodyLockInterface(), hit.mBodyID);
      if (lock.Succeeded()) {
        const JPH::Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(
            hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction));
        const float slope_rad = std::acos(std::clamp(n.GetY(), -1.f, 1.f));
        const float max_rad = p.max_slope_deg * 3.14159265f / 180.f;
        if (slope_rad > max_rad + 1e-3f) {
          bodies.SetPosition(id, start, JPH::EActivation::Activate);
          return Status::Ok();
        }
      }
    }

    // Step-up retry if horizontal blocked.
    if (p.max_step_height > 1e-4f &&
        (std::fabs(displacement.x) + std::fabs(displacement.z)) > 1e-4f) {
      const float moved_h = std::fabs(static_cast<float>(after.GetX() - start.GetX())) +
                            std::fabs(static_cast<float>(after.GetZ() - start.GetZ()));
      const float want_h = std::fabs(displacement.x) + std::fabs(displacement.z);
      if (moved_h < 0.15f * want_h) {
        bodies.SetPosition(id, start, JPH::EActivation::Activate);
        Vec3 stepped = displacement;
        stepped.y += p.max_step_height;
        (void)MoveCharacter(body_id, stepped);
      }
    }
    return Status::Ok();
  }

  std::vector<int> QueryTriggerOverlaps(const Vec3& center, const Vec3& half) const override {
    std::vector<int> all = OverlapAabb(center, half);
    std::vector<int> out;
    out.reserve(all.size());
    for (int id : all) {
      if (id >= 0 && id < static_cast<int>(body_is_trigger_.size()) &&
          body_is_trigger_[static_cast<std::size_t>(id)]) {
        out.push_back(id);
      }
    }
    return out;
  }

  void SetBodyTrigger(int body_id, bool is_trigger) override {
    if (body_id < 0 || body_id >= static_cast<int>(body_is_trigger_.size())) {
      return;
    }
    body_is_trigger_[static_cast<std::size_t>(body_id)] = is_trigger;
    const JPH::BodyID id = body_ids_[static_cast<std::size_t>(body_id)];
    physics_.GetBodyInterface().SetIsSensor(id, is_trigger);
  }

  bool IsBodyTrigger(int body_id) const override {
    if (body_id < 0 || body_id >= static_cast<int>(body_is_trigger_.size())) {
      return false;
    }
    return body_is_trigger_[static_cast<std::size_t>(body_id)];
  }

  Vec3 body_position(int body_id) const override {
    if (body_id < 0 || body_id >= static_cast<int>(body_ids_.size())) {
      return {};
    }
    const JPH::BodyID id = body_ids_[static_cast<std::size_t>(body_id)];
    const JPH::RVec3 p = physics_.GetBodyInterface().GetPosition(id);
    return {static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
            static_cast<float>(p.GetZ())};
  }

  Vec3 body_half_extents(int body_id) const override {
    if (body_id < 0 || body_id >= static_cast<int>(body_ids_.size())) {
      return {};
    }
    return half_extents_[static_cast<std::size_t>(body_id)];
  }

  int body_count() const override { return static_cast<int>(body_ids_.size()); }

  const char* backend_name() const override { return "jolt"; }

  bool ApplyImpulse(int body_id, const Vec3& impulse) override {
    if (body_id < 0 || body_id >= static_cast<int>(body_ids_.size())) {
      return false;
    }
    const JPH::BodyID id = body_ids_[static_cast<std::size_t>(body_id)];
    physics_.GetBodyInterface().AddImpulse(
        id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    return true;
  }

  std::vector<ContactPair> ConsumeContacts() override {
    if (!contacts_) {
      return IPhysicsWorld::ConsumeContacts();
    }
    return contacts_->Take();
  }

  int CreateSoftBody(const SoftBodyDesc& desc) override {
    const int grid = std::clamp(desc.grid, 2, 16);
    const float cell = desc.cell > 1e-4f ? desc.cell : 0.2f;

    JPH::Ref<JPH::SoftBodySharedSettings> shared =
        JPH::SoftBodySharedSettings::sCreateCube(static_cast<JPH::uint>(grid), cell);
    if (shared == nullptr || shared->mVertices.empty()) {
      return -1;
    }

    // Distribute total mass across free vertices (invMass > 0).
    if (desc.mass > 0.f) {
      JPH::uint free_count = 0;
      for (const auto& v : shared->mVertices) {
        if (v.mInvMass > 0.f) {
          ++free_count;
        }
      }
      if (free_count > 0) {
        const float inv = static_cast<float>(free_count) / desc.mass;
        for (auto& v : shared->mVertices) {
          if (v.mInvMass > 0.f) {
            v.mInvMass = inv;
          }
        }
      }
    }

    JPH::SoftBodyCreationSettings settings(
        shared, JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
        JPH::Quat::sIdentity(), Layers::kMoving);
    // Soft bodies are not in body_ids_; keep raycast user-data mapping unambiguous.
    settings.mUserData = static_cast<JPH::uint64>(-2);

    JPH::BodyInterface& bodies = physics_.GetBodyInterface();
    const JPH::BodyID id = bodies.CreateAndAddSoftBody(settings, JPH::EActivation::Activate);
    if (id.IsInvalid()) {
      return -1;
    }

    std::vector<std::uint32_t> indices;
    indices.reserve(shared->mFaces.size() * 3);
    for (const auto& face : shared->mFaces) {
      indices.push_back(face.mVertex[0]);
      indices.push_back(face.mVertex[1]);
      indices.push_back(face.mVertex[2]);
    }

    const int soft_id = static_cast<int>(soft_body_ids_.size());
    soft_body_ids_.push_back(id);
    soft_body_indices_.push_back(std::move(indices));
    return soft_id;
  }

  bool SoftBodyGetVertices(int id, std::vector<Vec3>& out_world) override {
    out_world.clear();
    if (id < 0 || id >= static_cast<int>(soft_body_ids_.size())) {
      return false;
    }
    const JPH::BodyID body_id = soft_body_ids_[static_cast<std::size_t>(id)];
    JPH::BodyLockRead lock(physics_.GetBodyLockInterface(), body_id);
    if (!lock.Succeeded()) {
      return false;
    }
    const JPH::Body& body = lock.GetBody();
    if (!body.IsSoftBody()) {
      return false;
    }
    const auto* mp =
        static_cast<const JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
    if (mp == nullptr) {
      return false;
    }
    const JPH::RMat44 com = body.GetCenterOfMassTransform();
    const auto& verts = mp->GetVertices();
    out_world.reserve(verts.size());
    for (const auto& v : verts) {
      const JPH::RVec3 p = com * v.mPosition;
      out_world.push_back({static_cast<float>(p.GetX()), static_cast<float>(p.GetY()),
                           static_cast<float>(p.GetZ())});
    }
    return !out_world.empty();
  }

  int SoftBodyGetIndexCount(int id) const override {
    if (id < 0 || id >= static_cast<int>(soft_body_indices_.size())) {
      return 0;
    }
    return static_cast<int>(soft_body_indices_[static_cast<std::size_t>(id)].size());
  }

  bool SoftBodyGetIndices(int id, std::vector<std::uint32_t>& out) override {
    out.clear();
    if (id < 0 || id >= static_cast<int>(soft_body_indices_.size())) {
      return false;
    }
    out = soft_body_indices_[static_cast<std::size_t>(id)];
    return !out.empty();
  }

  int CreateJoint(const JointDesc& desc) override {
    if (desc.body_a < 0 || desc.body_b < 0 ||
        desc.body_a >= static_cast<int>(body_ids_.size()) ||
        desc.body_b >= static_cast<int>(body_ids_.size())) {
      return -1;
    }
    const JPH::BodyID ids[2] = {body_ids_[static_cast<std::size_t>(desc.body_a)],
                                body_ids_[static_cast<std::size_t>(desc.body_b)]};
    JPH::BodyLockMultiWrite lock(physics_.GetBodyLockInterface(), ids, 2);
    JPH::Body* body_a = lock.GetBody(0);
    JPH::Body* body_b = lock.GetBody(1);
    if (!body_a || !body_b) {
      return -1;
    }

    JPH::TwoBodyConstraint* constraint = nullptr;
    const JPH::RVec3 wa(desc.anchor_a.x, desc.anchor_a.y, desc.anchor_a.z);
    const JPH::RVec3 wb(desc.anchor_b.x, desc.anchor_b.y, desc.anchor_b.z);
    JPH::Vec3 axis(desc.axis.x, desc.axis.y, desc.axis.z);
    if (axis.LengthSq() < 1e-8f) {
      axis = JPH::Vec3(0, 1, 0);
    } else {
      axis = axis.Normalized();
    }

    if (desc.type == JointType::Fixed) {
      JPH::FixedConstraintSettings settings;
      settings.mAutoDetectPoint = false;
      settings.mPoint1 = wa;
      settings.mPoint2 = wb;
      constraint = settings.Create(*body_a, *body_b);
    } else if (desc.type == JointType::Slider) {
      JPH::SliderConstraintSettings settings;
      settings.mAutoDetectPoint = false;
      settings.mPoint1 = wa;
      settings.mPoint2 = wb;
      settings.SetSliderAxis(axis);
      constraint = settings.Create(*body_a, *body_b);
    } else if (desc.type == JointType::BallSocket) {
      JPH::PointConstraintSettings settings;
      settings.mPoint1 = wa;
      settings.mPoint2 = wb;
      constraint = settings.Create(*body_a, *body_b);
    } else {
      JPH::HingeConstraintSettings settings;
      settings.mPoint1 = wa;
      settings.mPoint2 = wb;
      settings.mHingeAxis1 = axis;
      settings.mHingeAxis2 = axis;
      constraint = settings.Create(*body_a, *body_b);
    }
    if (!constraint) {
      return -1;
    }
    physics_.AddConstraint(constraint);
    const int id = static_cast<int>(joint_ptrs_.size());
    joint_ptrs_.push_back(constraint);
    joints_.push_back(desc);
    return id;
  }

  bool DestroyJoint(int joint_id) override {
    if (joint_id < 0 || joint_id >= static_cast<int>(joint_ptrs_.size())) {
      return false;
    }
    if (joint_ptrs_[static_cast<std::size_t>(joint_id)]) {
      physics_.RemoveConstraint(joint_ptrs_[static_cast<std::size_t>(joint_id)]);
      joint_ptrs_[static_cast<std::size_t>(joint_id)] = nullptr;
    }
    joints_[static_cast<std::size_t>(joint_id)].body_a = -1;
    return true;
  }

  int joint_count() const override {
    int n = 0;
    for (auto* p : joint_ptrs_) {
      if (p) {
        ++n;
      }
    }
    return n;
  }

  bool JointIsActive(int joint_id) const override {
    return joint_id >= 0 && joint_id < static_cast<int>(joint_ptrs_.size()) &&
           joint_ptrs_[static_cast<std::size_t>(joint_id)] != nullptr;
  }

  void SetCollisionLayer(int body_id, std::uint32_t layer) override {
    if (body_id >= 0 && body_id < static_cast<int>(body_layers_.size())) {
      body_layers_[static_cast<std::size_t>(body_id)] = layer;
    }
  }
  void SetCollisionMask(int body_id, std::uint32_t mask) override {
    if (body_id >= 0 && body_id < static_cast<int>(body_masks_.size())) {
      body_masks_[static_cast<std::size_t>(body_id)] = mask;
    }
  }
  std::uint32_t GetCollisionLayer(int body_id) const override {
    if (body_id >= 0 && body_id < static_cast<int>(body_layers_.size())) {
      return body_layers_[static_cast<std::size_t>(body_id)];
    }
    return 1u;
  }
  std::uint32_t GetCollisionMask(int body_id) const override {
    if (body_id >= 0 && body_id < static_cast<int>(body_masks_.size())) {
      return body_masks_[static_cast<std::size_t>(body_id)];
    }
    return 0xFFFFFFFFu;
  }

  RayHit ShapeCast(const Vec3& origin, const Vec3& half_extents, const Vec3& motion,
                   std::uint32_t mask) const override {
    RayHit out;
    JPH::RefConst<JPH::Shape> shape =
        new JPH::BoxShape(JPH::Vec3(half_extents.x, half_extents.y, half_extents.z));
    const JPH::RMat44 start = JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z));
    const JPH::RShapeCast shape_cast(shape, JPH::Vec3::sOne(), start,
                                     JPH::Vec3(motion.x, motion.y, motion.z));
    JPH::ShapeCastSettings settings;
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    physics_.GetNarrowPhaseQuery().CastShape(shape_cast, settings,
                                             JPH::RVec3(origin.x, origin.y, origin.z), collector);
    if (!collector.HadHit()) {
      return out;
    }
    out.hit = true;
    const float motion_len = motion.length();
    out.distance = collector.mHit.mFraction * motion_len;
    out.point = {origin.x + motion.x * collector.mHit.mFraction,
                 origin.y + motion.y * collector.mHit.mFraction,
                 origin.z + motion.z * collector.mHit.mFraction};
    out.normal = {0, 1, 0};
    out.body_id = -1;
    JPH::BodyLockRead lock(physics_.GetBodyLockInterface(), collector.mHit.mBodyID2);
    if (lock.Succeeded()) {
      const JPH::uint64 user = lock.GetBody().GetUserData();
      if (user != static_cast<JPH::uint64>(-1) && user < body_ids_.size()) {
        out.body_id = static_cast<int>(user);
        if (out.body_id >= 0 && out.body_id < static_cast<int>(body_layers_.size())) {
          if ((mask & body_layers_[static_cast<std::size_t>(out.body_id)]) == 0) {
            return {};
          }
        }
      }
    }
    return out;
  }

  bool IsOnFloor(int body_id) const override {
    return body_id >= 0 && body_id < static_cast<int>(char_on_floor_.size()) &&
           char_on_floor_[static_cast<std::size_t>(body_id)];
  }
  Vec3 GetFloorNormal(int body_id) const override {
    if (body_id >= 0 && body_id < static_cast<int>(char_floor_n_.size())) {
      return char_floor_n_[static_cast<std::size_t>(body_id)];
    }
    return {0, 1, 0};
  }

  std::vector<AreaEvent3D> ConsumeAreaEvents() override {
    if (!contacts_) {
      return {};
    }
    return contacts_->TakeAreaEvents();
  }

  int CreateRagdoll(const std::vector<RagdollBoneDesc>& bones) override {
    int created = 0;
    for (const auto& b : bones) {
      if (b.body_id < 0 || b.parent_body_id < 0) {
        continue;
      }
      JointDesc j;
      j.body_a = b.parent_body_id;
      j.body_b = b.body_id;
      j.type = b.joint;
      j.anchor_a = body_position(b.parent_body_id);
      j.anchor_b = body_position(b.body_id);
      if (CreateJoint(j) >= 0) {
        ++created;
      }
    }
    return created;
  }

  int CreateVehicle(const VehicleDesc& desc) override {
    RigidBodyDesc chassis;
    chassis.position = desc.position;
    chassis.half_extents = desc.chassis_half;
    chassis.mass = desc.mass > 0.f ? desc.mass : 1200.f;
    const int id = CreateBox(chassis);
    if (id < 0) {
      return -1;
    }
    vehicles_.push_back({id, desc.wheel_radius, desc.suspension, 0.f, 0.f});
    return id;
  }

  bool SetVehicleInput(int vehicle_id, float throttle, float steer) override {
    for (auto& v : vehicles_) {
      if (v.chassis_id == vehicle_id) {
        v.throttle = throttle;
        v.steer = steer;
        // Simple impulse drive along local +Z with lateral steer nudge.
        Vec3 drive{steer * 200.f, 0.f, throttle * 800.f};
        ApplyImpulse(vehicle_id, drive);
        return true;
      }
    }
    return false;
  }

  int ShatterBody(const BreakableDesc& desc) override {
    if (desc.body_id < 0 || desc.body_id >= static_cast<int>(body_ids_.size())) {
      return 0;
    }
    const Vec3 p = body_position(desc.body_id);
    const Vec3 h = body_half_extents(desc.body_id);
    const int n = std::clamp(desc.fragment_count, 2, 16);
    int made = 0;
    for (int i = 0; i < n; ++i) {
      RigidBodyDesc frag;
      frag.position = {p.x + ((i & 1) ? h.x * 0.35f : -h.x * 0.35f),
                       p.y + h.y * 0.2f,
                       p.z + ((i & 2) ? h.z * 0.35f : -h.z * 0.35f)};
      frag.half_extents = {h.x * 0.35f, h.y * 0.35f, h.z * 0.35f};
      frag.mass = 1.f;
      const int fid = CreateBox(frag);
      if (fid < 0) {
        continue;
      }
      Vec3 imp{(frag.position.x - p.x) * desc.impulse, desc.impulse * 0.5f,
               (frag.position.z - p.z) * desc.impulse};
      ApplyImpulse(fid, imp);
      ++made;
    }
    return made;
  }

 private:
  struct VehicleState {
    int chassis_id = -1;
    float wheel_radius = 0.35f;
    float suspension = 2.5f;
    float throttle = 0.f;
    float steer = 0.f;
  };
  BPLayerInterfaceImpl broadphase_layers_;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_;
  ObjectLayerPairFilterImpl object_vs_object_;
  JPH::PhysicsSystem physics_;
  std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator_;
  std::unique_ptr<JPH::JobSystemSingleThreaded> job_system_;
  std::vector<JPH::BodyID> body_ids_;
  std::vector<Vec3> half_extents_;
  std::vector<bool> body_is_trigger_;
  std::vector<JPH::BodyID> soft_body_ids_;
  std::vector<std::vector<std::uint32_t>> soft_body_indices_;
  JPH::BodyID floor_id_;
  std::unique_ptr<ContactCollector> contacts_;
  std::vector<JointDesc> joints_;
  std::vector<JPH::TwoBodyConstraint*> joint_ptrs_;
  std::vector<std::uint32_t> body_layers_;
  std::vector<std::uint32_t> body_masks_;
  std::vector<bool> char_on_floor_;
  std::vector<Vec3> char_floor_n_;
  std::vector<VehicleState> vehicles_;
};

}  // namespace

std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld() {
  return std::make_unique<JoltWorld>();
}

}  // namespace engine::physics
