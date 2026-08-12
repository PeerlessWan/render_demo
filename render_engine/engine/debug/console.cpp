#include "engine/debug/console.h"

#include <chrono>
#include <sstream>

namespace engine::debug {
namespace {

double NowSeconds() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

}  // namespace

void Console::Register(std::string_view command, ConsoleHandler handler) {
  handlers_[std::string(command)] = std::move(handler);
}

std::vector<std::string> Console::Tokenize(std::string_view line) {
  std::vector<std::string> tokens;
  std::istringstream ss{std::string(line)};
  std::string tok;
  while (ss >> tok) {
    tokens.push_back(tok);
  }
  return tokens;
}

Status Console::Execute(std::string_view line) {
  const auto tokens = Tokenize(line);
  if (tokens.empty()) {
    return Status::Ok();
  }
  const auto it = handlers_.find(tokens[0]);
  if (it == handlers_.end()) {
    return Status::Fail(ErrorCode::NotFound, "unknown command: " + tokens[0]);
  }
  return it->second(tokens);
}

void Profiler::Begin(const char* name) { open_[name] = NowSeconds(); }

void Profiler::End(const char* name) {
  const auto it = open_.find(name);
  if (it == open_.end()) {
    return;
  }
  samples_ms_[name] = (NowSeconds() - it->second) * 1000.0;
  open_.erase(it);
}

double Profiler::last_ms(const char* name) const {
  const auto it = samples_ms_.find(name);
  return it == samples_ms_.end() ? 0.0 : it->second;
}

}  // namespace engine::debug
