#pragma once

#include "engine/script/i_script_host.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace game_kit {

class GameRuntime;

// C19 thin adapter: engine stays VM-free; this forwards Tick/errors to game_kit.
class GameKitScriptHost final : public engine::script::IScriptHost {
 public:
  explicit GameKitScriptHost(GameRuntime* rt = nullptr) : rt_(rt) {}

  void set_runtime(GameRuntime* rt) { rt_ = rt; }

  engine::Status RegisterFunction(std::string_view name, engine::script::ScriptNativeFn fn) override;
  void Tick(float dt) override;
  void OnScriptError(std::string_view message) override;

  [[nodiscard]] bool has(std::string_view name) const;

 private:
  GameRuntime* rt_ = nullptr;
  std::unordered_map<std::string, engine::script::ScriptNativeFn> fns_;
};

}  // namespace game_kit
