#include "ui/hud.h"

#include "sim/blocks.h"
#include "sim/crafting.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace mc {
namespace {

void Quad(std::vector<engine::rhi::ScreenQuad>* quads, float x0, float y0, float x1, float y1,
          engine::ColorRgba c) {
  engine::rhi::ScreenQuad q;
  q.x0 = x0;
  q.y0 = y0;
  q.x1 = x1;
  q.y1 = y1;
  q.color = c;
  quads->push_back(q);
}

// 7-segment digit for stack counts (RetainedUi has no glyphs).
void Digit(std::vector<engine::rhi::ScreenQuad>* quads, float x, float y, int d) {
  static const int kSeg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  if (d < 0 || d > 9) {
    return;
  }
  const int m = kSeg[d];
  const float w = 6.f;
  const float h = 10.f;
  const engine::ColorRgba c{1, 1, 1, 0.95f};
  auto seg = [&](int bit, float x0, float y0, float x1, float y1) {
    if (m & bit) {
      Quad(quads, x0, y0, x1, y1, c);
    }
  };
  seg(1, x + 1, y, x + w - 1, y + 1.5f);              // a
  seg(2, x + w - 1.5f, y + 1, x + w, y + h * 0.5f);   // b
  seg(4, x + w - 1.5f, y + h * 0.5f, x + w, y + h);   // c
  seg(8, x + 1, y + h - 1.5f, x + w - 1, y + h);      // d
  seg(16, x, y + h * 0.5f, x + 1.5f, y + h);          // e
  seg(32, x, y + 1, x + 1.5f, y + h * 0.5f);          // f
  seg(64, x + 1, y + h * 0.5f - 0.8f, x + w - 1, y + h * 0.5f + 0.8f);  // g
}

void Number(std::vector<engine::rhi::ScreenQuad>* quads, float x, float y, int n) {
  if (n <= 0) {
    return;
  }
  if (n >= 100) {
    n = 99;
  }
  if (n >= 10) {
    Digit(quads, x, y, n / 10);
    Digit(quads, x + 8.f, y, n % 10);
  } else {
    Digit(quads, x + 8.f, y, n);
  }
}

int GlyphRow(char ch, int row) {
  if (row < 0 || row > 6) {
    return 0;
  }
  char c = ch;
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 32);
  }
  auto rows = [&](int r0, int r1, int r2, int r3, int r4, int r5, int r6) {
    const int v[] = {r0, r1, r2, r3, r4, r5, r6};
    return v[row];
  };
  switch (c) {
    case '0':
      return rows(0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e);
    case '1':
      return rows(0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e);
    case '2':
      return rows(0x0e, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1f);
    case '3':
      return rows(0x0e, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0e);
    case '4':
      return rows(0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02);
    case 'A':
      return rows(0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11);
    case 'B':
      return rows(0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e);
    case 'C':
      return rows(0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e);
    case 'D':
      return rows(0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e);
    case 'E':
      return rows(0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f);
    case 'F':
      return rows(0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10);
    case 'G':
      return rows(0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e);
    case 'H':
      return rows(0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11);
    case 'I':
      return rows(0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e);
    case 'K':
      return rows(0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11);
    case 'L':
      return rows(0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f);
    case 'M':
      return rows(0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11);
    case 'N':
      return rows(0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11);
    case 'O':
      return rows(0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e);
    case 'P':
      return rows(0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10);
    case 'Q':
      return rows(0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d);
    case 'R':
      return rows(0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11);
    case 'S':
      return rows(0x0e, 0x11, 0x10, 0x0e, 0x01, 0x11, 0x0e);
    case 'T':
      return rows(0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04);
    case 'U':
      return rows(0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e);
    case 'V':
      return rows(0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04);
    case 'W':
      return rows(0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11);
    case 'X':
      return rows(0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11);
    case 'Y':
      return rows(0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04);
    case '-':
      return rows(0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00);
    case '.':
      return rows(0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06);
    default:
      return 0;
  }
}

void DrawText(std::vector<engine::rhi::ScreenQuad>* quads, float x, float y, const char* s, float px,
              engine::ColorRgba tint) {
  if (!quads || !s) {
    return;
  }
  float cx = x;
  for (const char* p = s; *p; ++p) {
    if (*p == ' ') {
      cx += px * 4.f;
      continue;
    }
    for (int row = 0; row < 7; ++row) {
      const int bits = GlyphRow(*p, row);
      for (int bit = 0; bit < 5; ++bit) {
        if (bits & (1 << (4 - bit))) {
          Quad(quads, cx + static_cast<float>(bit) * px, y + static_cast<float>(row) * px,
               cx + static_cast<float>(bit + 1) * px, y + static_cast<float>(row + 1) * px, tint);
        }
      }
    }
    cx += px * 6.f;
  }
}

void DrawSlot(std::vector<engine::rhi::ScreenQuad>* quads, std::vector<SlotHit>* hits, SlotHit::Kind kind,
              int index, float x, float y, float s, const Stack& st, bool selected) {
  Quad(quads, x, y, x + s, y + s,
       selected ? engine::ColorRgba{1, 1, 1, 0.45f} : engine::ColorRgba{0.12f, 0.12f, 0.14f, 0.85f});
  if (!st.empty()) {
    auto col = GetDef(st.id).color;
    col.a = 1.f;
    Quad(quads, x + 4.f, y + 4.f, x + s - 4.f, y + s - 4.f, col);
    Number(quads, x + 2.f, y + s - 14.f, st.count);
    const int max_d = MaxDurability(st.id);
    if (max_d > 0) {
      const float fill = 1.f - static_cast<float>(st.wear) / static_cast<float>(max_d);
      Quad(quads, x + 3.f, y + s - 5.f, x + 3.f + (s - 6.f) * fill, y + s - 3.f, {0.2f, 0.85f, 0.3f, 1.f});
    }
  }
  if (hits) {
    SlotHit hit;
    hit.kind = kind;
    hit.index = index;
    hit.x0 = x;
    hit.y0 = y;
    hit.x1 = x + s;
    hit.y1 = y + s;
    hits->push_back(hit);
  }
}

void AppendUiRects(engine::ui::RetainedUi& ui, std::vector<engine::rhi::ScreenQuad>* quads) {
  for (const auto& r : ui.BuildDrawList()) {
    Quad(quads, r.x0, r.y0, r.x1, r.y1, r.color);
  }
}

}  // namespace

void BuildHud(engine::ui::RetainedUi& ui, const Player& p, const Containers* boxes, int w, int h,
              const HudParams& params, std::vector<engine::rhi::ScreenQuad>* quads,
              std::vector<SlotHit>* hits) {
  if (!quads) {
    return;
  }
  quads->clear();
  ui.Clear();
  if (hits) {
    hits->clear();
  }
  const float fw = static_cast<float>(w);
  const float fh = static_cast<float>(h);
  const float cx = fw * 0.5f;
  const float cy = fh * 0.5f;

  if (params.in_menu) {
    ui.Panel("menu", cx - 200.f, cy - 180.f, 400.f, 380.f);
    ui.Button("menu_new", "New Survival", cx - 140.f, cy - 80.f, 280.f, 40.f);
    ui.Button("menu_load", "Load slot0", cx - 140.f, cy - 28.f, 280.f, 40.f);
    ui.Button("menu_creative", "Creative", cx - 140.f, cy + 24.f, 280.f, 40.f);
    ui.Button("menu_quit", "Quit", cx - 140.f, cy + 76.f, 280.f, 40.f);
    AppendUiRects(ui, quads);
    DrawText(quads, cx - 90.f, cy - 150.f, "MINECRAFT", 3.5f, {1.f, 1.f, 1.f, 1.f});
    DrawText(quads, cx - 70.f, cy - 118.f, "SURVIVAL", 2.5f, {0.75f, 0.9f, 0.7f, 1.f});
    DrawText(quads, cx - 100.f, cy - 70.f, "1  NEW WORLD", 2.4f, {1.f, 1.f, 1.f, 1.f});
    DrawText(quads, cx - 100.f, cy - 18.f, "2  LOAD SAVE", 2.4f, {1.f, 1.f, 1.f, 1.f});
    DrawText(quads, cx - 100.f, cy + 34.f, "3  CREATIVE", 2.4f, {1.f, 1.f, 1.f, 1.f});
    DrawText(quads, cx - 100.f, cy + 86.f, "4  QUIT", 2.4f, {1.f, 0.85f, 0.85f, 1.f});
    DrawText(quads, cx - 170.f, cy + 140.f, "WASD MOVE   RMB LOOK   LMB DIG", 1.8f,
             {0.75f, 0.78f, 0.82f, 1.f});
    return;
  }

  Quad(quads, cx - 2.f, cy - 8.f, cx + 2.f, cy + 8.f, {1, 1, 1, 0.9f});
  Quad(quads, cx - 8.f, cy - 2.f, cx + 8.f, cy + 2.f, {1, 1, 1, 0.9f});

  if (p.breaking && params.break_need > 0.01f) {
    const float t = std::min(p.break_acc / params.break_need, 1.f);
    Quad(quads, cx - 40.f, cy + 16.f, cx + 40.f, cy + 22.f, {0.05f, 0.05f, 0.06f, 0.7f});
    Quad(quads, cx - 40.f, cy + 16.f, cx - 40.f + 80.f * t, cy + 22.f, {0.95f, 0.85f, 0.2f, 0.95f});
  }

  const Stack held = p.inv.Hotbar();
  if (!held.empty() && IsBlock(held.id)) {
    auto col = GetDef(held.id).color;
    col.a = 0.85f;
    Quad(quads, fw - 64.f, fh - 64.f, fw - 24.f, fh - 24.f, col);
  }

  const float bar_y = fh - 96.f;
  Quad(quads, 24.f, bar_y, 204.f, bar_y + 14.f, {0.05f, 0.05f, 0.06f, 0.7f});
  Quad(quads, 24.f, bar_y, 24.f + 180.f * (p.hp / 20.f), bar_y + 14.f, {0.82f, 0.18f, 0.18f, 0.9f});
  Quad(quads, 24.f, bar_y + 18.f, 204.f, bar_y + 32.f, {0.05f, 0.05f, 0.06f, 0.7f});
  Quad(quads, 24.f, bar_y + 18.f, 24.f + 180.f * (p.hunger / 20.f), bar_y + 32.f,
       {0.82f, 0.62f, 0.18f, 0.9f});

  const float slot = 40.f;
  const float hx = cx - 4.5f * slot;
  const float hy = fh - 52.f;
  for (int i = 0; i < Inventory::kHotbar; ++i) {
    DrawSlot(quads, hits, SlotHit::Kind::Hotbar, i, hx + i * slot, hy, slot - 4.f, p.inv.slots[i],
             i == p.inv.selected);
  }

  DrawText(quads, 24.f, 16.f, p.creative ? "CREATIVE" : (p.dead ? "DEAD" : "SURVIVAL"), 2.2f,
           {1.f, 1.f, 1.f, 0.95f});

  if (params.f3) {
    DrawText(quads, 24.f, 40.f, "F3 DEBUG", 2.f, {0.8f, 0.9f, 1.f, 1.f});
  }

  if (p.dead) {
    ui.Panel("dead", cx - 160.f, cy - 60.f, 320.f, 120.f, {0.12f, 0.04f, 0.04f, 0.9f});
    ui.Label("deadt", "R respawn", cx - 40.f, cy - 8.f);
    DrawText(quads, cx - 90.f, cy - 10.f, "R  RESPAWN", 3.f, {1.f, 0.85f, 0.85f, 1.f});
  }
  if (params.paused) {
    ui.Panel("pause", cx - 120.f, cy - 40.f, 240.f, 80.f);
    ui.Label("pauset", "PAUSED (P)", cx - 40.f, cy - 8.f);
    DrawText(quads, cx - 70.f, cy - 8.f, "PAUSED  P", 3.f, {1.f, 1.f, 1.f, 1.f});
  }

  if (p.ui_open) {
    ui.Panel("inv", 72.f, 64.f, 520.f, 420.f);
    const char* title = p.ui == Player::Ui::Table     ? "CRAFTING TABLE"
                        : p.ui == Player::Ui::Chest   ? "CHEST"
                        : p.ui == Player::Ui::Furnace ? "FURNACE"
                                                      : "INVENTORY";
    DrawText(quads, 88.f, 76.f, title, 2.2f, {1.f, 1.f, 1.f, 1.f});
    const float s = 36.f;
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 9; ++c) {
        const int idx = r * 9 + c;
        DrawSlot(quads, hits, SlotHit::Kind::Inv, idx, 96.f + c * s, 280.f + r * s, 32.f, p.inv.slots[idx],
                 false);
      }
    }
    const bool show_craft = p.ui == Player::Ui::Inventory || p.ui == Player::Ui::Table;
    if (show_craft) {
      const int craft_n = p.ui == Player::Ui::Table ? 9 : 4;
      const int cols = p.ui == Player::Ui::Table ? 3 : 2;
      for (int i = 0; i < craft_n; ++i) {
        DrawSlot(quads, hits, SlotHit::Kind::Craft, i, 96.f + (i % cols) * s, 110.f + (i / cols) * s, 32.f,
                 p.craft_grid[i], false);
      }
      Stack grid[9]{};
      for (int i = 0; i < 9; ++i) {
        grid[i] = p.craft_grid[i];
      }
      Stack result{};
      (void)Craft(grid, p.ui == Player::Ui::Table, &result);
      DrawSlot(quads, hits, SlotHit::Kind::Result, 0, 250.f, 128.f, 36.f, result, false);
    }

    if (p.ui == Player::Ui::Chest && boxes) {
      const auto it = boxes->chests.find(BlockPos{p.ui_x, p.ui_y, p.ui_z});
      const ChestData empty{};
      const ChestData& chest = it == boxes->chests.end() ? empty : it->second;
      for (int i = 0; i < 27; ++i) {
        DrawSlot(quads, hits, SlotHit::Kind::Chest, i, 96.f + (i % 9) * s, 110.f + (i / 9) * s, 32.f,
                 chest.slots[i], false);
      }
    }
    if (p.ui == Player::Ui::Furnace && boxes) {
      const auto it = boxes->furnaces.find(BlockPos{p.ui_x, p.ui_y, p.ui_z});
      FurnaceData f{};
      if (it != boxes->furnaces.end()) {
        f = it->second;
      }
      DrawSlot(quads, hits, SlotHit::Kind::FurnaceIn, 0, 120.f, 120.f, 36.f, f.input, false);
      DrawSlot(quads, hits, SlotHit::Kind::FurnaceFuel, 0, 120.f, 170.f, 36.f, f.fuel, false);
      DrawSlot(quads, hits, SlotHit::Kind::FurnaceOut, 0, 220.f, 145.f, 36.f, f.output, false);
    }
    if (p.creative) {
      const Id pal_ids[] = {Id::Stone,     Id::Dirt,          Id::Grass, Id::Sand,     Id::Gravel,
                        Id::OakLog,    Id::OakLeaves,     Id::OakPlanks, Id::Cobble,   Id::CoalOre,
                        Id::IronOre,   Id::CraftingTable, Id::Furnace,   Id::Chest,    Id::Glass,
                        Id::Water,     Id::Torch,         Id::Bed};
      for (int i = 0; i < 18; ++i) {
        Stack pal;
        pal.id = pal_ids[i];
        pal.count = 1;
        DrawSlot(quads, hits, SlotHit::Kind::Palette, i, 430.f + (i % 3) * 36.f, 110.f + (i / 3) * 36.f, 32.f,
                 pal, false);
      }
    }
  }

  if (!p.inv.cursor.empty()) {
    auto col = GetDef(p.inv.cursor.id).color;
    col.a = 1.f;
    const float mx = params.mouse_x;
    const float my = params.mouse_y;
    Quad(quads, mx - 12.f, my - 12.f, mx + 12.f, my + 12.f, col);
    Number(quads, mx - 10.f, my + 4.f, p.inv.cursor.count);
  }

  AppendUiRects(ui, quads);
}

}  // namespace mc
