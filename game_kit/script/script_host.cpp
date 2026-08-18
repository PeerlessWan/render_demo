#include "game_kit/script_host.h"

#include "game_kit/runtime.h"
#include "game_kit/script.h"

#include "engine/core/log.h"

namespace game_kit {

engine::Status GameKitScriptHost::RegisterFunction(std::string_view name,
                                                   engine::script::ScriptNativeFn fn) {
  fns_[std::string(name)] = std::move(fn);
  return engine::Status::Ok();
}

void GameKitScriptHost::Tick(float dt) {
  if (rt_ && rt_->script()) {
    (void)rt_->script()->CallUpdate(dt);
  }
}

void GameKitScriptHost::OnScriptError(std::string_view message) {
  engine::LogError("IScriptHost: " + std::string(message));
  if (rt_ && rt_->script()) {
    rt_->script()->Freeze(message);
  }
}

bool GameKitScriptHost::has(std::string_view name) const {
  return fns_.find(std::string(name)) != fns_.end();
}

}  // namespace game_kit
