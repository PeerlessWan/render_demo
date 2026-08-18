#include "sim/gameplay.h"

#include "sim/blocks.h"
#include "sim/player.h"
#include "world/chunk.h"
#include "world/tick.h"

#include "engine/core/math.h"

#include <algorithm>
#include <cmath>

namespace mc {
namespace {

Stack* SlotPtr(GameState* st, const SlotHit& hit) {
  if (!st) {
    return nullptr;
  }
  switch (hit.kind) {
    case SlotHit::Kind::Inv:
    case SlotHit::Kind::Hotbar:
      if (hit.index >= 0 && hit.index < Inventory::kSize) {
        return &st->player.inv.slots[hit.index];
      }
      break;
    case SlotHit::Kind::Craft:
      if (hit.index >= 0 && hit.index < 9) {
        return &st->player.craft_grid[hit.index];
      }
      break;
    case SlotHit::Kind::Chest: {
      auto& c = st->boxes.ChestAt(st->player.ui_x, st->player.ui_y, st->player.ui_z);
      if (hit.index >= 0 && hit.index < 27) {
        return &c.slots[hit.index];
      }
      break;
    }
    case SlotHit::Kind::FurnaceIn:
      return &st->boxes.FurnaceAt(st->player.ui_x, st->player.ui_y, st->player.ui_z).input;
    case SlotHit::Kind::FurnaceFuel:
      return &st->boxes.FurnaceAt(st->player.ui_x, st->player.ui_y, st->player.ui_z).fuel;
    case SlotHit::Kind::FurnaceOut:
      return &st->boxes.FurnaceAt(st->player.ui_x, st->player.ui_y, st->player.ui_z).output;
    default:
      break;
  }
  return nullptr;
}

void SpawnDrop(GameState* st, engine::Vec3 pos, Id id, int n) {
  if (!st || id == Id::Air || n <= 0) {
    return;
  }
  Drop d;
  d.pos = pos;
  d.id = id;
  d.count = static_cast<std::uint8_t>(n < 64 ? n : 64);
  d.life = 24.f;
  st->drops.push_back(d);
}

const Id kPalette[] = {Id::Stone,         Id::Dirt,          Id::Grass,     Id::Sand,     Id::Gravel,
                       Id::OakLog,        Id::OakLeaves,     Id::OakPlanks, Id::Cobble,   Id::CoalOre,
                       Id::IronOre,       Id::CraftingTable, Id::Furnace,   Id::Chest,    Id::Glass,
                       Id::Water,         Id::Torch,         Id::Bed,       Id::Coal,     Id::Stick,
                       Id::WoodenPickaxe, Id::StonePickaxe,  Id::WoodenAxe, Id::StoneAxe, Id::WoodenShovel,
                       Id::StoneShovel,   Id::WoodenSword,   Id::StoneSword, Id::Bread,   Id::CookedBeef};

}  // namespace

void ApplySlotClick(GameState* st, const SlotHit& hit) {
  if (!st) {
    return;
  }
  if (hit.kind == SlotHit::Kind::Palette) {
    const int n = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    if (hit.index >= 0 && hit.index < n && st->player.creative) {
      st->player.inv.cursor.id = kPalette[hit.index];
      st->player.inv.cursor.count = IsTool(kPalette[hit.index]) ? 1 : 64;
      st->player.inv.cursor.wear = 0;
    }
    return;
  }
  if (hit.kind == SlotHit::Kind::Result) {
    const bool table = st->player.ui == Player::Ui::Table;
    Stack out{};
    if (!Craft(st->player.craft_grid, table, &out) || out.empty()) {
      return;
    }
    Stack& cur = st->player.inv.cursor;
    if (!cur.empty() && (cur.id != out.id || MaxDurability(out.id) > 0)) {
      return;
    }
    if (cur.empty()) {
      cur = out;
    } else {
      const int room = 64 - cur.count;
      if (out.count > room) {
        return;
      }
      cur.count = static_cast<std::uint8_t>(cur.count + out.count);
    }
    ConsumeCraft(st->player.craft_grid, table);
    return;
  }
  if (Stack* slot = SlotPtr(st, hit)) {
    ClickStacks(slot, &st->player.inv.cursor);
  }
}

void TickDrops(GameState* st, float dt) {
  if (!st) {
    return;
  }
  for (auto& d : st->drops) {
    d.life -= dt;
    const int x = static_cast<int>(std::floor(d.pos.x));
    const int y = static_cast<int>(std::floor(d.pos.y - 0.05f));
    const int z = static_cast<int>(std::floor(d.pos.z));
    if (!IsSolid(st->world.Get(x, y, z))) {
      d.pos.y -= 6.f * dt;
    }
    const engine::Vec3 rel = d.pos - st->player.pos;
    if (rel.length() < 1.6f && d.life > 0.f) {
      if (st->player.inv.Add(d.id, d.count)) {
        d.life = 0.f;
      }
    }
  }
  st->drops.erase(std::remove_if(st->drops.begin(), st->drops.end(),
                                 [](const Drop& d) { return d.life <= 0.f; }),
                  st->drops.end());
}

void CollectTorchLights(const World& world, const engine::Vec3& eye, int radius,
                        std::vector<engine::render::LocalLight>* lights) {
  if (!lights) {
    return;
  }
  lights->clear();
  ChunkCoord center{};
  WorldToChunk(static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.z)), &center,
               nullptr, nullptr);
  int id = 1;
  for (int dz = -radius; dz <= radius; ++dz) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const Chunk* ch = world.FindChunk(ChunkCoord{center.x + dx, center.z + dz});
      if (!ch) {
        continue;
      }
      const int ox = (center.x + dx) * kChunkW;
      const int oz = (center.z + dz) * kChunkW;
      for (int y = 0; y < kChunkH; ++y) {
        for (int lz = 0; lz < kChunkW; ++lz) {
          for (int lx = 0; lx < kChunkW; ++lx) {
            if (ch->Get(lx, y, lz) != Id::Torch) {
              continue;
            }
            engine::render::LocalLight L;
            L.id = id++;
            L.position = {static_cast<float>(ox + lx) + 0.5f, static_cast<float>(y) + 0.7f,
                          static_cast<float>(oz + lz) + 0.5f};
            L.range = 14.f;
            L.color = {1.f, 0.82f, 0.45f, 1.f};
            L.intensity = 2.2f;
            L.cast_shadows = false;
            L.spot_angle_deg = 180.f;
            lights->push_back(L);
            if (static_cast<int>(lights->size()) >= engine::render::kMaxLocalLightsGpu) {
              return;
            }
          }
        }
      }
    }
  }
}

void CollectDrops(const GameState& st, std::vector<engine::rhi::LitDrawItem>* items) {
  if (!items) {
    return;
  }
  items->clear();
  for (const auto& d : st.drops) {
    engine::rhi::LitDrawItem item;
    item.world = engine::Mat4::TRS(d.pos, engine::Quat::Identity(), engine::Vec3{0.28f, 0.28f, 0.28f});
    item.use_albedo = false;
    item.mesh_slot = 0;
    item.color = GetDef(d.id).color;
    item.color.a = 1.f;
    items->push_back(item);
  }
}

bool TickGameplay(GameState* st, const engine::render::Camera& cam, const GameInput& in, float dt,
                  bool* paused, bool* f3, const std::vector<SlotHit>& ui_hits) {
  if (!st) {
    return false;
  }
  if (in.toggle_pause && paused) {
    *paused = !*paused;
  }
  if (in.toggle_f3 && f3) {
    *f3 = !*f3;
  }
  if (paused && *paused) {
    return false;
  }
  if (st->player.dead) {
    if (in.eat_or_respawn) {
      RespawnAtSpawn(st->world, &st->player);
    }
    return false;
  }
  if (in.toggle_inv) {
    st->player.ui_open = !st->player.ui_open;
    st->player.ui = st->player.ui_open ? Player::Ui::Inventory : Player::Ui::None;
  }
  if (in.toggle_creative) {
    st->player.creative = !st->player.creative;
    st->player.flying = st->player.creative;
    if (st->player.creative) {
      FillCreativeInventory(&st->player.inv);
    }
  }
  if (in.view_in) {
    st->view_radius = std::min(st->view_radius + 1, 6);
  }
  if (in.view_out) {
    st->view_radius = std::max(st->view_radius - 1, 2);
  }
  if (in.snap) {
    for (int k = 0; k < 9; ++k) {
      if (in.snap->keys[0x31 + k]) {
        st->player.inv.selected = k;
      }
    }
  }
  if (st->player.ui_open && in.lmb_pressed) {
    for (const auto& hit : ui_hits) {
      if (in.mouse_x >= hit.x0 && in.mouse_x < hit.x1 && in.mouse_y >= hit.y0 && in.mouse_y < hit.y1) {
        ApplySlotClick(st, hit);
        break;
      }
    }
  }

  if (!st->player.ui_open) {
    TickPlayer(st->world, &st->player, cam, dt, in.wish, in.jump, in.sneak);
  }

  const int wx = static_cast<int>(std::floor(st->player.pos.x));
  const int wz = static_cast<int>(std::floor(st->player.pos.z));
  st->world.StreamAround(wx, wz, 4);
  TickFallingAndWater(st->world, wx, wz, 2);

  st->clock.Tick(dt);
  if (!st->player.creative) {
    st->player.hunger -= dt * 0.05f;
    if (st->player.hunger < 0.f) {
      st->player.hunger = 0.f;
      Hurt(&st->player, dt * 1.5f);
    } else if (st->player.hunger > 16.f && st->player.hp < 20.f) {
      st->player.hp += dt * 0.5f;
      if (st->player.hp > 20.f) {
        st->player.hp = 20.f;
      }
    }
  }
  st->boxes.Tick(dt);
  TickMobs(st->world, &st->player, st->clock, &st->mobs, dt);
  TickDrops(st, dt);

  if (in.eat_or_respawn && !st->player.dead) {
    TryEat(&st->player);
  }

  const TraceHit hit = TraceBlocks(st->world, Eye(st->player), LookDir(cam), 6.f);
  if (!st->player.ui_open) {
    if (in.lmb) {
      Id drop_id = Id::Air;
      engine::Vec3 drop_pos{};
      if (HitMob(&st->mobs, &st->player, Eye(st->player), LookDir(cam), &drop_id, &drop_pos)) {
        if (drop_id != Id::Air) {
          SpawnDrop(st, drop_pos, drop_id, 1);
        }
        WearStack(&st->player.inv.slots[st->player.inv.selected], 1);
      } else {
        const Id before = hit.hit ? st->world.Get(hit.x, hit.y, hit.z) : Id::Air;
        if (TryBreak(st->world, &st->player, hit, dt)) {
          st->boxes.Remove(hit.x, hit.y, hit.z);
          if (!st->player.creative) {
            const auto& d = GetDef(before);
            if (d.drop != Id::Air) {
              SpawnDrop(st,
                        {static_cast<float>(hit.x) + 0.5f, static_cast<float>(hit.y) + 0.4f,
                         static_cast<float>(hit.z) + 0.5f},
                        d.drop, d.drop_count);
            }
          }
        }
      }
    } else {
      st->player.breaking = false;
      st->player.break_acc = 0.f;
    }
    if (in.mmb_pressed && hit.hit && st->player.creative) {
      const Id id = st->world.Get(hit.x, hit.y, hit.z);
      if (id != Id::Air && id != Id::Bedrock) {
        st->player.inv.slots[st->player.inv.selected].id = id;
        st->player.inv.slots[st->player.inv.selected].count = 64;
        st->player.inv.slots[st->player.inv.selected].wear = 0;
      }
    }
    if (in.rmb_pressed && hit.hit) {
      const Id id = st->world.Get(hit.x, hit.y, hit.z);
      if (id == Id::CraftingTable) {
        st->player.ui_open = true;
        st->player.ui = Player::Ui::Table;
      } else if (id == Id::Chest) {
        st->player.ui_open = true;
        st->player.ui = Player::Ui::Chest;
        st->player.ui_x = hit.x;
        st->player.ui_y = hit.y;
        st->player.ui_z = hit.z;
      } else if (id == Id::Furnace) {
        st->player.ui_open = true;
        st->player.ui = Player::Ui::Furnace;
        st->player.ui_x = hit.x;
        st->player.ui_y = hit.y;
        st->player.ui_z = hit.z;
      } else if (id == Id::Bed) {
        st->player.spawn = st->player.pos;
        st->player.has_spawn = true;
        if (st->clock.Night()) {
          st->clock.SleepToDawn();
        }
      } else {
        TryPlace(st->world, &st->player, hit);
      }
    }
  }
  if (st->player.pos.y < -2.f && !st->player.dead) {
    Hurt(&st->player, 20.f);
  }
  return in.save;
}

}  // namespace mc
