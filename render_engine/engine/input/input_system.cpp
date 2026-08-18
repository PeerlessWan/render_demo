#include "engine/input/input_system.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace engine::input {

void ActionMap::Bind(std::string_view action, std::string_view binding) {
  bindings_[std::string(action)] = std::string(binding);
}

bool ActionMap::is_bound(std::string_view action) const {
  return bindings_.find(std::string(action)) != bindings_.end();
}

const std::string* ActionMap::binding(std::string_view action) const {
  const auto it = bindings_.find(std::string(action));
  return it == bindings_.end() ? nullptr : &it->second;
}

Status ActionMap::SaveToFile(const std::filesystem::path& path) const {
  std::ofstream out(path);
  if (!out) {
    return Status::Fail("cannot write action map: " + path.string());
  }
  out << "{\n";
  bool first = true;
  for (const auto& [k, v] : bindings_) {
    if (!first) {
      out << ",\n";
    }
    first = false;
    out << "  \"" << k << "\": \"" << v << "\"";
  }
  out << "\n}\n";
  return Status::Ok();
}

Status ActionMap::LoadFromFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    return Status::Fail("cannot read action map: " + path.string());
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string text = ss.str();
  bindings_.clear();
  std::size_t pos = 0;
  while (true) {
    const auto q1 = text.find('"', pos);
    if (q1 == std::string::npos) {
      break;
    }
    const auto q2 = text.find('"', q1 + 1);
    if (q2 == std::string::npos) {
      break;
    }
    const auto q3 = text.find('"', q2 + 1);
    if (q3 == std::string::npos) {
      break;
    }
    const auto q4 = text.find('"', q3 + 1);
    if (q4 == std::string::npos) {
      break;
    }
    Bind(text.substr(q1 + 1, q2 - q1 - 1), text.substr(q3 + 1, q4 - q3 - 1));
    pos = q4 + 1;
  }
  return Status::Ok();
}

void InputSystem::set_key(Key key, bool down) {
  const auto i = static_cast<std::size_t>(key);
  if (i < keys_.size()) {
    keys_[i] = down;
  }
}

void InputSystem::set_mouse_delta(float dx, float dy) { mouse_delta_ = Vec2{dx, dy}; }

void InputSystem::set_gamepad_axis(int axis, float value) {
  if (axis >= 0 && axis < 2) {
    pad_axes_[axis] = value;
  }
}

void InputSystem::set_gamepad_button(int button, bool down) {
  if (button >= 0 && button < 8) {
    pad_buttons_[button] = down;
  }
}

void InputSystem::BeginFrame() {
  // keep previous keys from EndFrame
}

void InputSystem::EndFrame() {
  keys_prev_ = keys_;
  mouse_delta_ = {};
}

bool InputSystem::key_down(Key key) const {
  const auto i = static_cast<std::size_t>(key);
  return i < keys_.size() && keys_[i];
}

bool InputSystem::key_pressed(Key key) const {
  const auto i = static_cast<std::size_t>(key);
  return i < keys_.size() && keys_[i] && !keys_prev_[i];
}

void InputSystem::InstallFlyCameraDefaults() {
  actions_.Bind("MoveX", "Axis:KeyAD+Pad0");
  actions_.Bind("MoveY", "Axis:KeyQE");
  actions_.Bind("MoveZ", "Axis:KeyWS+Pad1");
  actions_.Bind("LookX", "Axis:MouseX");
  actions_.Bind("LookY", "Axis:MouseY");
  actions_.Bind("Jump", "Button:Space");
}

float InputSystem::axis(std::string_view action) const {
  const auto it = action_states_.find(std::string(action));
  return it == action_states_.end() ? 0.f : it->second.value;
}

bool InputSystem::pressed(std::string_view action) const {
  const auto it = action_states_.find(std::string(action));
  return it != action_states_.end() && it->second.pressed;
}

void InputSystem::EvaluateActions() {
  auto set_axis = [&](const char* name, float v) {
    ActionState& s = action_states_[name];
    s.value = v;
    s.pressed = std::fabs(v) > 0.2f;
  };
  auto set_button = [&](const char* name, bool down) {
    ActionState& s = action_states_[name];
    const bool was = s.pressed;
    s.pressed = down;
    s.value = down ? 1.f : 0.f;
    s.just_pressed = down && !was;
    s.just_released = !down && was;
  };

  float move_x = (key_down(Key::D) ? 1.f : 0.f) + (key_down(Key::A) ? -1.f : 0.f) + pad_axes_[0];
  float move_z = (key_down(Key::W) ? 1.f : 0.f) + (key_down(Key::S) ? -1.f : 0.f) + pad_axes_[1];
  float move_y = (key_down(Key::E) ? 1.f : 0.f) + (key_down(Key::Q) ? -1.f : 0.f);
  set_axis("MoveX", move_x);
  set_axis("MoveY", move_y);
  set_axis("MoveZ", move_z);
  set_axis("LookX", mouse_delta_.x);
  set_axis("LookY", mouse_delta_.y);
  set_button("Jump", key_down(Key::Space) || pad_buttons_[0]);
}

}  // namespace engine::input
