#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine::input {

enum class Key : int {
  Unknown = 0,
  W,
  A,
  S,
  D,
  Q,
  E,
  Space,
  Escape,
  Count,
};

struct ActionState {
  float value = 0.f;
  bool pressed = false;
  bool just_pressed = false;
  bool just_released = false;
};

class ActionMap {
 public:
  void Bind(std::string_view action, std::string_view binding);
  [[nodiscard]] bool is_bound(std::string_view action) const;
  [[nodiscard]] const std::string* binding(std::string_view action) const;

  Status SaveToFile(const std::filesystem::path& path) const;
  Status LoadFromFile(const std::filesystem::path& path);

 private:
  std::unordered_map<std::string, std::string> bindings_;
};

class InputSystem {
 public:
  void set_key(Key key, bool down);
  void set_mouse_delta(float dx, float dy);
  void set_mouse_wheel(float notches) { mouse_wheel_ = notches; }
  void set_gamepad_axis(int axis, float value);  // 0=LX 1=LY
  void set_gamepad_button(int button, bool down);

  void BeginFrame();
  void EndFrame();

  ActionMap& action_map() { return actions_; }
  const ActionMap& action_map() const { return actions_; }

  [[nodiscard]] bool key_down(Key key) const;
  // True on the frame the key transitions down (uses keys_prev_ from EndFrame).
  [[nodiscard]] bool key_pressed(Key key) const;
  [[nodiscard]] Vec2 mouse_delta() const { return mouse_delta_; }
  [[nodiscard]] float mouse_wheel() const { return mouse_wheel_; }
  [[nodiscard]] float axis(std::string_view action) const;
  [[nodiscard]] bool pressed(std::string_view action) const;

  void InstallFlyCameraDefaults();
  void EvaluateActions();

 private:
  std::array<bool, static_cast<std::size_t>(Key::Count)> keys_{};
  std::array<bool, static_cast<std::size_t>(Key::Count)> keys_prev_{};
  Vec2 mouse_delta_{};
  float mouse_wheel_ = 0.f;
  float pad_axes_[2]{};
  bool pad_buttons_[8]{};
  ActionMap actions_;
  std::unordered_map<std::string, ActionState> action_states_;
};

}  // namespace engine::input
