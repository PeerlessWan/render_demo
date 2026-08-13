#include "engine/physics/i_physics_world.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <cstdarg>
#include <cstdio>
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
    float y = static_cast<float>(cur.GetY()) + displacement.y;
    const float floor_y = half_extents_[static_cast<std::size_t>(body_id)].y;
    if (y < floor_y) {
      y = floor_y;
    }
    bodies.SetPosition(id,
                       JPH::RVec3(cur.GetX() + displacement.x, y, cur.GetZ() + displacement.z),
                       JPH::EActivation::Activate);
    return Status::Ok();
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

 private:
  BPLayerInterfaceImpl broadphase_layers_;
  ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_;
  ObjectLayerPairFilterImpl object_vs_object_;
  JPH::PhysicsSystem physics_;
  std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator_;
  std::unique_ptr<JPH::JobSystemSingleThreaded> job_system_;
  std::vector<JPH::BodyID> body_ids_;
  std::vector<Vec3> half_extents_;
  JPH::BodyID floor_id_;
};

}  // namespace

std::unique_ptr<IPhysicsWorld> CreateJoltPhysicsWorld() {
  return std::make_unique<JoltWorld>();
}

}  // namespace engine::physics
