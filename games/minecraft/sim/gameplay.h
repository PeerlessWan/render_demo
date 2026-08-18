#pragma once

#include "io/world_save.h"
#include "sim/crafting.h"
#include "world/trace.h"

#include "engine/core/math.h"
#include "engine/platform/window.h"
#include "engine/render/camera.h"
#include "engine/render/local_lights.h"
#include "engine/rhi/i_device.h"

#include <vector>

namespace mc {

struct GameInput {
  const engine::WindowInputSnapshot* snap = nullptr;
  engine::Vec3 wish{};
  bool jump = false;
  bool sneak = false;
  bool lmb = false;
  bool rmb = false;
  bool mmb = false;
  bool lmb_pressed = false;
  bool rmb_pressed = false;
  bool mmb_pressed = false;
  float mouse_x = 0.f;
  float mouse_y = 0.f;
  bool toggle_pause = false;
  bool toggle_inv = false;
  bool toggle_f3 = false;
  bool toggle_creative = false;
  bool eat_or_respawn = false;
  bool save = false;
  bool view_in = false;
  bool view_out = false;
};

struct SlotHit {
  enum class Kind { Inv, Craft, Result, Chest, FurnaceIn, FurnaceFuel, FurnaceOut, Palette, Hotbar };
  Kind kind = Kind::Inv;
  int index = 0;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct HudParams {
  bool paused = false;
  bool in_menu = false;
  bool f3 = false;
  float break_need = 1.f;
  TraceHit look{};
  Id look_id = Id::Air;
  float yaw = 0.f;
  float pitch = 0.f;
  int view_radius = 3;
  float mouse_x = 0.f;
  float mouse_y = 0.f;
};

void TickDrops(GameState* st, float dt);
void CollectTorchLights(const World& world, const engine::Vec3& eye, int radius,
                        std::vector<engine::render::LocalLight>* lights);
void CollectDrops(const GameState& st, std::vector<engine::rhi::LitDrawItem>* items);

// Returns true if a world save was requested this frame.
bool TickGameplay(GameState* st, const engine::render::Camera& cam, const GameInput& in, float dt,
                  bool* paused, bool* f3, const std::vector<SlotHit>& ui_hits);

void ApplySlotClick(GameState* st, const SlotHit& hit);

}  // namespace mc
