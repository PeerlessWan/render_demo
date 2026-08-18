#pragma once

#include "engine/core/result.h"
#include "engine/scene/world.h"

#include "game_kit/entity.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace game_kit {

class GameRuntime;

class DapSession;

struct ScriptStackFrame {
  std::string name;
  int line = 0;
};

class ScriptDebugger {
 public:
  void AddBreakpoint(std::string chunk, int line);
  void Step() { step_ = true; }
  void Continue() { waiting_ = false; }
  void set_on_break(std::function<void(ScriptDebugger&)> cb) { on_break_ = std::move(cb); }
  void set_dap(DapSession* dap) { dap_ = dap; }
  [[nodiscard]] int line() const { return line_; }
  [[nodiscard]] const std::string& chunk() const { return chunk_; }
  [[nodiscard]] std::string GetLocal(std::string_view name) const;
  [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& locals() const {
    return locals_;
  }
  [[nodiscard]] const std::vector<ScriptStackFrame>& stack() const { return stack_; }
  [[nodiscard]] bool waiting() const { return waiting_; }
  void OnLine(void* lua_state, int line, std::string_view src);

 private:
  struct Bp {
    std::string chunk;
    int line = 0;
  };
  std::vector<Bp> bps_;
  std::function<void(ScriptDebugger&)> on_break_;
  std::vector<std::pair<std::string, std::string>> locals_;
  std::vector<ScriptStackFrame> stack_;
  std::string chunk_;
  int line_ = 0;
  bool step_ = false;
  bool waiting_ = false;
  DapSession* dap_ = nullptr;
};

class ScriptVm {
 public:
  ScriptVm();
  ~ScriptVm();

  ScriptVm(const ScriptVm&) = delete;
  ScriptVm& operator=(const ScriptVm&) = delete;
  ScriptVm(ScriptVm&&) noexcept;
  ScriptVm& operator=(ScriptVm&&) noexcept;

  void Attach(engine::scene::World* world, GameRuntime* rt);
  void BindSelf(engine::scene::NodeId node, EntityId entity = kInvalidEntity);
  void set_debug_hooks(bool enabled);
  void set_instruction_budget(int n);
  void NoteLine(int line, std::string_view chunk);
  bool ConsumeBudget();

  engine::Status LoadString(std::string_view source, std::string_view chunk_name);
  engine::Status LoadFile(std::string_view path);
  engine::Status CallNamed(std::string_view name);
  engine::Status CallUpdate(float dt);
  engine::Status CallTrigger(bool enter, std::string_view other);
  void Reset();
  void Freeze(std::string_view message);

  [[nodiscard]] bool available() const;
  [[nodiscard]] bool frozen() const { return frozen_; }
  [[nodiscard]] const std::string& last_error() const { return last_error_; }
  [[nodiscard]] const std::string& last_traceback() const { return last_traceback_; }
  [[nodiscard]] int last_line() const { return last_line_; }
  [[nodiscard]] const std::string& last_chunk() const { return last_chunk_; }
  [[nodiscard]] void* lua_state() const;
  [[nodiscard]] ScriptDebugger& debugger() { return debugger_; }

  engine::Status CallNamed1(std::string_view name, std::string_view arg);

  [[nodiscard]] std::string DumpPersist() const;
  engine::Status RestorePersist(std::string_view blob);

 private:
  void RefreshHooks();

  struct Impl;
  std::unique_ptr<Impl> impl_;
  ScriptDebugger debugger_;
  bool frozen_ = false;
  std::string last_error_;
  std::string last_traceback_;
  int instruction_budget_ = 0;
  int last_line_ = 0;
  std::string last_chunk_;
};

}  // namespace game_kit
