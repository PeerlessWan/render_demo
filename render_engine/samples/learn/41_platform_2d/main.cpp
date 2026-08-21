#include "engine/core/log.h"
#include "engine/physics/i_physics_world2d.h"
#include "engine/render2d/animation_player2d.h"
#include "engine/render2d/camera2d.h"
#include "engine/render2d/canvas_item.h"
#include "engine/render2d/sprite_batch.h"
#include "engine/render2d/tile_draw.h"

#include <cstdlib>
#include <string>
#include <vector>

// ADR 0049: 2D platform demo — CharacterBody2D + Camera2D + Canvas + Anim (headless OK).
int main(int argc, char** argv) {
  int frames = 30;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i] ? argv[i] : "";
    if (a.rfind("--headless_frames=", 0) == 0) {
      frames = std::atoi(a.c_str() + 18);
    }
  }

  auto phys = engine::physics::CreateDefaultPhysicsWorld2D();
  if (!phys) {
    engine::LogError("Physics2D create failed");
    return 1;
  }
  engine::LogInfo(std::string("Physics2D: ") + phys->backend_name());

  engine::physics::Body2DDesc ground;
  ground.position = {0.f, 8.f};
  ground.shape.half_extents = {40.f, 1.f};
  ground.collision_layer = 1u;
  (void)phys->CreateStaticBody2D(ground);

  engine::physics::Body2DDesc hero;
  hero.position = {0.f, 5.f};
  hero.shape.half_extents = {0.4f, 0.6f};
  hero.collision_mask = 1u;
  const int player = phys->CreateCharacterBody2D(hero);

  engine::render2d::CanvasItemTree canvas;
  const int root = canvas.Create("world");
  const int spr = canvas.Create("hero", root);
  canvas.get(spr)->size = {16.f, 24.f};
  canvas.get(spr)->color = {0.2f, 0.9f, 0.4f, 1.f};

  engine::render2d::Camera2D cam;
  cam.design_size = {320.f, 180.f};
  cam.follow_smoothing = 8.f;

  engine::render2d::AnimationPlayer2D anim;
  engine::render2d::AnimationClip2D bob;
  bob.name = "idle";
  bob.length = 1.f;
  bob.loop = true;
  engine::render2d::AnimTrack2D tr;
  tr.type = engine::render2d::AnimTrackType2D::Modulate;
  tr.target_item = spr;
  tr.keys.push_back({0.f, 1.f, 1.f, 1.f, 1.f, 0});
  tr.keys.push_back({0.5f, 0.7f, 1.f, 0.7f, 1.f, 0});
  tr.keys.push_back({1.f, 1.f, 1.f, 1.f, 1.f, 0});
  bob.tracks.push_back(tr);
  anim.AddClip(std::move(bob));
  anim.Play("idle");

  engine::render2d::AtlasBank bank;
  bank.by_id["tiles"] = {engine::render2d::AtlasFrame{"g", 0, 0, 1, 1}};
  engine::render2d::TilemapLayer layer;
  layer.width = 4;
  layer.height = 1;
  layer.tile_w = 16;
  layer.tile_h = 16;
  layer.gids = {1, 1, 1, 1};
  std::vector<engine::render2d::Sprite> tiles;
  engine::render2d::ExpandTileLayerToSprites(layer, "tiles", bank.by_id["tiles"], 0, &tiles);

  float vx = 20.f;
  for (int f = 0; f < frames; ++f) {
    const float dt = 1.f / 60.f;
    anim.Step(dt);
    std::vector<engine::render2d::AnimationPlayer2D::Sample> samples;
    anim.SampleActive(&samples);
    for (const auto& s : samples) {
      if (auto* it = canvas.get(s.item)) {
        if (s.has_mod) {
          it->modulate = s.modulate;
        }
      }
    }
    (void)phys->MoveAndSlide(player, {vx, 40.f}, dt);
    const auto p = phys->body_position(player);
    if (auto* it = canvas.get(spr)) {
      it->position = {p.x * 16.f, p.y * 16.f};
    }
    engine::render2d::Camera2DFollow(&cam, {p.x * 16.f, p.y * 16.f}, dt);
    std::vector<engine::render2d::Sprite> sprites;
    canvas.FlattenSprites(&sprites);
    std::vector<engine::rhi::TexturedQuad> quads;
    engine::render2d::BuildTexturedQuads(sprites, bank, &quads);
    (void)quads;
    (void)tiles;
    phys->Step(dt);
  }

  engine::LogInfo("platform_2d ok floor=" +
                  std::string(phys->IsOnFloor(player) ? "1" : "0") +
                  " sprites_ok=1 tiles=" + std::to_string(tiles.size()));
  return 0;
}
