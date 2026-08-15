#include "engine/render2d/tilemap_stream.h"

#include <cmath>

namespace engine::render2d {
namespace {

float LerpRotKeys(const std::vector<BoneRotKey2D>& keys, float t) {
  if (keys.empty()) {
    return 0.f;
  }
  if (keys.size() == 1 || t <= keys.front().time) {
    return keys.front().rot;
  }
  if (t >= keys.back().time) {
    return keys.back().rot;
  }
  for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
    const auto& a = keys[i];
    const auto& b = keys[i + 1];
    if (t >= a.time && t <= b.time) {
      const float span = b.time - a.time;
      const float u = span > 1e-6f ? (t - a.time) / span : 0.f;
      return a.rot + (b.rot - a.rot) * u;
    }
  }
  return keys.back().rot;
}

}  // namespace

void TilemapStreamer::Configure(int map_w, int map_h, int chunk_size, std::size_t budget_chunks) {
  map_w_ = map_w;
  map_h_ = map_h;
  chunk_size_ = chunk_size > 0 ? chunk_size : 16;
  budget_ = budget_chunks > 0 ? budget_chunks : 1;
  gids_.assign(static_cast<std::size_t>(map_w_ * map_h_), 0);
  chunks_.clear();
  const int cw = (map_w_ + chunk_size_ - 1) / chunk_size_;
  const int ch = (map_h_ + chunk_size_ - 1) / chunk_size_;
  for (int cy = 0; cy < ch; ++cy) {
    for (int cx = 0; cx < cw; ++cx) {
      TileChunk c;
      c.chunk_x = cx;
      c.chunk_y = cy;
      c.size = chunk_size_;
      c.gids.assign(static_cast<std::size_t>(chunk_size_ * chunk_size_), 0);
      chunks_.push_back(std::move(c));
    }
  }
}

void TilemapStreamer::SetGid(int x, int y, int gid) {
  if (x < 0 || y < 0 || x >= map_w_ || y >= map_h_) {
    return;
  }
  gids_[static_cast<std::size_t>(y * map_w_ + x)] = gid;
  const int cx = x / chunk_size_;
  const int cy = y / chunk_size_;
  if (auto* c = const_cast<TileChunk*>(FindChunk(cx, cy))) {
    const int lx = x % chunk_size_;
    const int ly = y % chunk_size_;
    c->gids[static_cast<std::size_t>(ly * chunk_size_ + lx)] = gid;
  }
}

const TileChunk* TilemapStreamer::FindChunk(int cx, int cy) const {
  for (const auto& c : chunks_) {
    if (c.chunk_x == cx && c.chunk_y == cy) {
      return &c;
    }
  }
  return nullptr;
}

void TilemapStreamer::UpdateResidence(int focus_x, int focus_y, int radius_chunks) {
  const int fcx = focus_x / chunk_size_;
  const int fcy = focus_y / chunk_size_;
  for (auto& c : chunks_) {
    const int dx = c.chunk_x - fcx;
    const int dy = c.chunk_y - fcy;
    c.resident = (dx * dx + dy * dy) <= radius_chunks * radius_chunks;
  }
  // Enforce budget: drop farthest residents.
  while (resident_count() > budget_) {
    TileChunk* farthest = nullptr;
    int best = -1;
    for (auto& c : chunks_) {
      if (!c.resident) {
        continue;
      }
      const int dx = c.chunk_x - fcx;
      const int dy = c.chunk_y - fcy;
      const int d2 = dx * dx + dy * dy;
      if (d2 > best) {
        best = d2;
        farthest = &c;
      }
    }
    if (!farthest) {
      break;
    }
    farthest->resident = false;
  }
}

std::size_t TilemapStreamer::resident_count() const {
  std::size_t n = 0;
  for (const auto& c : chunks_) {
    if (c.resident) {
      ++n;
    }
  }
  return n;
}

void TilemapStreamer::ExpandResidentToSprites(std::vector<Sprite>& out,
                                             const TileExpandDesc& desc) const {
  for (const auto& c : chunks_) {
    if (!c.resident) {
      continue;
    }
    for (int ly = 0; ly < c.size; ++ly) {
      for (int lx = 0; lx < c.size; ++lx) {
        const int gid = c.gids[static_cast<std::size_t>(ly * c.size + lx)];
        if (desc.skip_zero_gid && gid == 0) {
          continue;
        }
        const int wx = c.chunk_x * c.size + lx;
        const int wy = c.chunk_y * c.size + ly;
        if (wx >= map_w_ || wy >= map_h_) {
          continue;
        }
        Sprite s;
        s.atlas_id = desc.atlas_id;
        s.frame = gid;
        s.position = {static_cast<float>(wx) * desc.tile_w, static_cast<float>(wy) * desc.tile_h};
        s.size = {desc.tile_w, desc.tile_h};
        s.sort_layer = desc.sort_layer;
        s.sort_y = s.position.y;
        s.nearest = true;
        s.color = desc.color;
        out.push_back(std::move(s));
      }
    }
  }
}

BonePose2D SampleSkeleton2D(const Skeleton2D& skel, float time) {
  BonePose2D pose;
  pose.positions.resize(skel.bones.size());
  pose.rotations.resize(skel.bones.size());
  for (std::size_t i = 0; i < skel.bones.size(); ++i) {
    const float wave = std::sin(time + static_cast<float>(i) * 0.5f) * 0.2f;
    pose.rotations[i] = skel.bones[i].bind_rot + wave;
    pose.positions[i] = skel.bones[i].bind_pos;
    if (skel.bones[i].parent >= 0) {
      const auto& p = pose.positions[static_cast<std::size_t>(skel.bones[i].parent)];
      pose.positions[i] = Vec2{p.x + skel.bones[i].bind_pos.x, p.y + skel.bones[i].bind_pos.y};
    }
  }
  return pose;
}

BonePose2D SampleSkeletonClip2D(const Skeleton2D& skel, const SkeletonClip2D& clip, float time) {
  float t = time;
  if (clip.duration > 1e-6f) {
    if (clip.loop) {
      t = std::fmod(t, clip.duration);
      if (t < 0.f) {
        t += clip.duration;
      }
    } else if (t > clip.duration) {
      t = clip.duration;
    }
  }

  BonePose2D pose;
  pose.positions.resize(skel.bones.size());
  pose.rotations.resize(skel.bones.size());
  for (std::size_t i = 0; i < skel.bones.size(); ++i) {
    float extra = 0.f;
    if (i < clip.channels.size()) {
      extra = LerpRotKeys(clip.channels[i], t);
    }
    pose.rotations[i] = skel.bones[i].bind_rot + extra;
    pose.positions[i] = skel.bones[i].bind_pos;
    if (skel.bones[i].parent >= 0) {
      const auto& p = pose.positions[static_cast<std::size_t>(skel.bones[i].parent)];
      const float rad = pose.rotations[static_cast<std::size_t>(skel.bones[i].parent)];
      const float c = std::cos(rad);
      const float s = std::sin(rad);
      const Vec2 local = skel.bones[i].bind_pos;
      pose.positions[i] = Vec2{p.x + c * local.x - s * local.y, p.y + s * local.x + c * local.y};
    }
  }
  return pose;
}

SkeletonClip2D MakeTinyWalkClip2D() {
  SkeletonClip2D clip;
  clip.name = "walk";
  clip.duration = 1.f;
  clip.loop = true;
  clip.channels.resize(2);
  // Root stays; child swings ±0.35 rad over the cycle.
  clip.channels[0] = {{0.f, 0.f}, {1.f, 0.f}};
  clip.channels[1] = {{0.f, -0.35f}, {0.5f, 0.35f}, {1.f, -0.35f}};
  return clip;
}

}  // namespace engine::render2d
