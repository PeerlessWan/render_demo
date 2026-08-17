#include "game_kit/runtime.h"

#include "game_kit/script.h"

#include "engine/app/application.h"

namespace game_kit {

GameRuntime::GameRuntime() : script_(std::make_unique<ScriptVm>()) {}

GameRuntime::~GameRuntime() = default;

void GameRuntime::Tick(engine::Application& app, float dt) {
  logic_dt_ = paused_ ? 0.f : dt;
  levels_.Pump(app, *this);
  if (logic_dt_ > 0.f) {
    timer_.Tick(logic_dt_);
    if (script_) {
      script_->Attach(&app.world(), this);
      (void)script_->CallUpdate(logic_dt_);
    }
  }
}

}  // namespace game_kit
