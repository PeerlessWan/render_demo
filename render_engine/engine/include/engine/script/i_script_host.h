#pragma once

#include "engine/core/result.h"

#include <functional>
#include <string>
#include <string_view>

namespace engine::script {

// C19 stub: optional script-host abstraction. Engine stays VM-free; game_kit/plugins own the VM.
// Default NullScriptHost is a no-op. See HOSTING.md §5.2 / ADR 0027.
using ScriptNativeFn = std::function<int(/*argc*/ int)>;

class IScriptHost {
 public:
  virtual ~IScriptHost() = default;

  virtual Status RegisterFunction(std::string_view name, ScriptNativeFn fn) = 0;
  virtual void Tick(float dt) = 0;
  virtual void OnScriptError(std::string_view message) = 0;
};

class NullScriptHost final : public IScriptHost {
 public:
  Status RegisterFunction(std::string_view /*name*/, ScriptNativeFn /*fn*/) override {
    return Status::Ok();
  }
  void Tick(float /*dt*/) override {}
  void OnScriptError(std::string_view /*message*/) override {}
};

}  // namespace engine::script
