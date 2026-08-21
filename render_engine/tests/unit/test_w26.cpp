#include "mini_test.h"

#include "engine/physics/i_physics_world.h"
#include "engine/physics/i_physics_world2d.h"
#include "engine/render2d/animation_player2d.h"
#include "engine/render2d/atlas.h"
#include "engine/render2d/camera2d.h"
#include "engine/render2d/canvas_item.h"
#include "engine/render2d/sprite_batch.h"
#include "engine/render2d/tile_draw.h"
#include "engine/rhi/i_device.h"

#include <cmath>
#include <memory>
#include <span>
#include <string>

using namespace engine;
using namespace engine::render2d;
using namespace engine::physics;

TEST_CASE("W26 canvas item tree parent transform and y-sort", "[w26]") {
  CanvasItemTree tree;
  const int root = tree.Create("root");
  const int child = tree.Create("child", root);
  tree.get(root)->position = {10.f, 0.f};
  tree.get(child)->position = {5.f, 2.f};
  tree.get(child)->y_sort = true;
  tree.get(child)->size = {8.f, 8.f};
  const int other = tree.Create("other", root);
  tree.get(other)->position = {5.f, 1.f};
  tree.get(other)->y_sort = true;
  tree.get(other)->size = {8.f, 8.f};

  std::vector<CanvasWorldTransform> world;
  tree.ComputeWorld(&world);
  REQUIRE(world.size() == 3);
  bool found = false;
  for (std::size_t i = 0; i < tree.items().size(); ++i) {
    if (tree.items()[i].id == child) {
      REQUIRE(std::fabs(world[i].position.x - 15.f) < 1e-3f);
      REQUIRE(std::fabs(world[i].position.y - 2.f) < 1e-3f);
      found = true;
    }
  }
  REQUIRE(found);

  std::vector<Sprite> sprites;
  tree.FlattenSprites(&sprites);
  REQUIRE(sprites.size() >= 2);
  REQUIRE(sprites[0].position.y <= sprites[1].position.y);
}

TEST_CASE("W26 Camera2D projection integer scale", "[w26]") {
  Camera2D cam;
  cam.design_size = {320.f, 180.f};
  cam.position = {0.f, 0.f};
  cam.integer_scale = true;
  cam.zoom = 1.f;
  const float ppu = Camera2DPixelsPerUnit(cam, 640.f, 360.f);
  REQUIRE(std::fabs(ppu - 2.f) < 1e-3f);
  const Vec2 scr = Camera2DWorldToScreen(cam, {10.f, 0.f}, 640.f, 360.f);
  REQUIRE(std::fabs(scr.x - 340.f) < 1e-3f);
  const Vec2 back = Camera2DScreenToWorld(cam, scr, 640.f, 360.f);
  REQUIRE(std::fabs(back.x - 10.f) < 1e-3f);
}

TEST_CASE("W26 sprite UV batch from atlas", "[w26]") {
  AtlasBank bank;
  bank.by_id["hero"] = {AtlasFrame{"0", 0.f, 0.f, 0.25f, 0.5f},
                        AtlasFrame{"1", 0.25f, 0.f, 0.5f, 0.5f}};
  Sprite spr;
  spr.atlas_id = "hero";
  spr.frame = 1;
  spr.position = {0.f, 0.f};
  spr.size = {16.f, 16.f};
  spr.color = {1, 1, 1, 1};
  std::vector<rhi::TexturedQuad> quads;
  BuildTexturedQuads(std::span<const Sprite>(&spr, 1), bank, &quads);
  REQUIRE(quads.size() == 1);
  REQUIRE(std::fabs(quads[0].u0 - 0.25f) < 1e-4f);
  REQUIRE(std::fabs(quads[0].u1 - 0.5f) < 1e-4f);
}

TEST_CASE("W26 Physics2D layers move_and_slide area joint", "[w26]") {
  auto world = CreateDefaultPhysicsWorld2D();
  REQUIRE(world);
  REQUIRE(std::string(world->backend_name()) == "builtin2d");

  Body2DDesc floor;
  floor.position = {0.f, 10.f};
  floor.shape.half_extents = {20.f, 1.f};
  floor.collision_layer = 1u;
  const int ground = world->CreateStaticBody2D(floor);
  REQUIRE(ground >= 0);

  Body2DDesc ch;
  ch.position = {0.f, 8.f};
  ch.shape.half_extents = {0.5f, 0.5f};
  ch.collision_mask = 1u;
  const int hero = world->CreateCharacterBody2D(ch);
  REQUIRE(hero >= 0);
  REQUIRE(world->MoveAndSlide(hero, {0.f, 20.f}, 0.1f));
  REQUIRE(world->IsOnFloor(hero));
  REQUIRE(world->GetCollisionMask(hero) == 1u);

  Body2DDesc area;
  area.position = {0.f, 8.f};
  area.shape.half_extents = {2.f, 2.f};
  area.collision_layer = 1u;
  area.collision_mask = 0xFFFFFFFFu;
  const int zone = world->CreateArea2D(area);
  world->Step(0.016f);
  auto ev = world->ConsumeAreaEvents();
  bool entered = false;
  for (const auto& e : ev) {
    if (e.area_id == zone && e.entered) {
      entered = true;
    }
  }
  REQUIRE(entered);

  Body2DDesc a, b;
  a.position = {-1.f, 0.f};
  a.shape.half_extents = {0.2f, 0.2f};
  a.mass = 1.f;
  b.position = {1.f, 0.f};
  b.shape.half_extents = {0.2f, 0.2f};
  b.mass = 1.f;
  const int ba = world->CreateRigidBody2D(a);
  const int bb = world->CreateRigidBody2D(b);
  Joint2DDesc j;
  j.type = JointType2D::Pin;
  j.body_a = ba;
  j.body_b = bb;
  const int jid = world->CreateJoint2D(j);
  REQUIRE(jid >= 0);
  REQUIRE(world->joint_count() == 1);
  world->Step(0.016f);
  REQUIRE(world->DestroyJoint2D(jid));
  REQUIRE(world->joint_count() == 0);
}

TEST_CASE("W26 Physics3D ShapeCast layers joint API", "[w26]") {
  auto world = CreateDefaultPhysicsWorld();
  REQUIRE(world);
  RigidBodyDesc box;
  box.position = {0.f, 1.f, 0.f};
  box.half_extents = {1.f, 1.f, 1.f};
  box.mass = 0.f;
  box.collision_layer = 1u;
  const int id = world->CreateBox(box);
  REQUIRE(id >= 0);
  world->SetCollisionLayer(id, 1u);
  REQUIRE(world->GetCollisionLayer(id) == 1u);

  RayHit hit = world->ShapeCast({0.f, 5.f, 0.f}, {0.2f, 0.2f, 0.2f}, {0.f, -4.f, 0.f}, 0xFFFFFFFFu);
  (void)hit;

  CapsuleDesc cap;
  cap.position = {3.f, 2.f, 0.f};
  cap.mass = 0.f;
  const int ca = world->CreateCapsule(cap);
  CapsuleDesc cap2 = cap;
  cap2.position = {3.5f, 2.f, 0.f};
  const int cb = world->CreateCapsule(cap2);
  IPhysicsWorld::JointDesc jd;
  jd.body_a = ca;
  jd.body_b = cb;
  jd.type = IPhysicsWorld::JointType::Hinge;
  jd.anchor_a = world->body_position(ca);
  jd.anchor_b = world->body_position(cb);
  const int jid = world->CreateJoint(jd);
  if (std::string(world->backend_name()) == "jolt") {
    REQUIRE(jid >= 0);
    REQUIRE(world->JointIsActive(jid));
    REQUIRE(world->joint_count() >= 1);
  } else {
    (void)jid;
  }
}

TEST_CASE("W26 AnimationPlayer2D and tile expand", "[w26]") {
  AnimationPlayer2D player;
  AnimationClip2D clip;
  clip.name = "bob";
  clip.length = 1.f;
  clip.loop = true;
  AnimTrack2D tr;
  tr.type = AnimTrackType2D::Position;
  tr.target_item = 1;
  tr.keys.push_back(AnimKey2D{0.f, 0.f, 0.f, 0.f, 0.f, 0});
  tr.keys.push_back(AnimKey2D{1.f, 10.f, 0.f, 0.f, 0.f, 0});
  clip.tracks.push_back(tr);
  player.AddClip(std::move(clip));
  REQUIRE(player.Play("bob"));
  player.Step(0.5f);
  std::vector<AnimationPlayer2D::Sample> samples;
  player.SampleActive(&samples);
  REQUIRE(samples.size() == 1);
  REQUIRE(samples[0].has_pos);
  REQUIRE(std::fabs(samples[0].position.x - 5.f) < 1e-3f);

  TilemapLayer layer;
  layer.width = 2;
  layer.height = 1;
  layer.tile_w = 16;
  layer.tile_h = 16;
  layer.gids = {1, 0};
  AtlasBank bank;
  bank.by_id["tiles"] = {AtlasFrame{"t0", 0, 0, 1, 1}};
  std::vector<Sprite> sprites;
  ExpandTileLayerToSprites(layer, "tiles", bank.by_id["tiles"], 0, &sprites);
  REQUIRE(sprites.size() == 1);
  REQUIRE(sprites[0].frame == 0);
}

TEST_CASE("W26 headless DrawTexturedQuads", "[w26]") {
  rhi::DeviceDesc desc;
  desc.width = 64;
  desc.height = 64;
  auto device = rhi::CreateHeadlessDevice(desc);
  REQUIRE(device);
  rhi::TexturedQuad q;
  q.x0 = 0;
  q.y0 = 0;
  q.x1 = 8;
  q.y1 = 8;
  q.u0 = 0;
  q.v0 = 0;
  q.u1 = 1;
  q.v1 = 1;
  REQUIRE(device.value()->DrawTexturedQuads(std::span<const rhi::TexturedQuad>(&q, 1)));
}
