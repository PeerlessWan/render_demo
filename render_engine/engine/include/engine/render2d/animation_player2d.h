#pragma once

#include "engine/core/math.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::render2d {

enum class AnimTrackType2D : std::uint8_t {
  Position = 0,
  Rotation = 1,
  Modulate = 2,
  SpriteFrame = 3,
};

struct AnimKey2D {
  float time = 0.f;
  float f0 = 0.f, f1 = 0.f, f2 = 0.f, f3 = 0.f;
  int i0 = 0;
};

struct AnimTrack2D {
  AnimTrackType2D type = AnimTrackType2D::Position;
  int target_item = -1;  // CanvasItem id
  std::vector<AnimKey2D> keys;
};

struct AnimationClip2D {
  std::string name;
  float length = 1.f;
  bool loop = true;
  std::vector<AnimTrack2D> tracks;
};

// Godot AnimationPlayer2D subset (ADR 0049): property + sprite frame tracks.
class AnimationPlayer2D {
 public:
  void AddClip(AnimationClip2D clip);
  [[nodiscard]] bool HasClip(const std::string& name) const;
  bool Play(const std::string& name);
  void Stop();
  void Step(float dt);

  // Apply evaluated values into out maps (caller writes into CanvasItemTree).
  struct Sample {
    int item = -1;
    bool has_pos = false;
    Vec2 position{};
    bool has_rot = false;
    float rotation = 0.f;
    bool has_mod = false;
    ColorRgba modulate{1, 1, 1, 1};
    bool has_frame = false;
    int frame = 0;
  };
  void SampleActive(std::vector<Sample>* out) const;

  [[nodiscard]] bool playing() const { return playing_; }
  [[nodiscard]] float time() const { return time_; }
  [[nodiscard]] const std::string& active_clip() const { return active_; }

 private:
  std::unordered_map<std::string, AnimationClip2D> clips_;
  std::string active_;
  float time_ = 0.f;
  bool playing_ = false;
};

}  // namespace engine::render2d
