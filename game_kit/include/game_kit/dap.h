#pragma once

#include "game_kit/script.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace game_kit {

// Debug Adapter Protocol subset over in-memory JSON (optional TCP listen).
class DapSession {
 public:
  void Attach(ScriptDebugger* dbg) { dbg_ = dbg; }
  [[nodiscard]] ScriptDebugger* debugger() const { return dbg_; }

  std::string Handle(std::string_view json);
  void Queue(std::string json) { inbox_.push_back(std::move(json)); }
  void Poll();

  void OnStopped(ScriptDebugger& dbg);
  void set_blocking(bool v) { blocking_ = v; }
  [[nodiscard]] bool blocking() const { return blocking_; }
  [[nodiscard]] const std::string& last_event() const { return last_event_; }
  [[nodiscard]] int last_line() const { return last_line_; }

  bool Listen(std::uint16_t port);
  void CloseListen();

 private:
  std::string Dispatch(std::string_view json);
  void ApplyBreakpoints(std::string_view json);

  ScriptDebugger* dbg_ = nullptr;
  std::deque<std::string> inbox_;
  std::string last_event_;
  int last_line_ = 0;
  int seq_ = 1;
  bool blocking_ = false;
  std::intptr_t listen_sock_ = -1;
};

}  // namespace game_kit
