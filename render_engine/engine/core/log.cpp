#include "engine/core/log.h"

#include <iostream>
#include <mutex>

namespace engine {
namespace {

std::mutex g_log_mutex;
LogLevel g_log_level = LogLevel::Info;
bool g_log_info_to_stderr = false;

const char* LevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "?";
}

}  // namespace

void set_log_level(LogLevel level) {
  std::lock_guard lock(g_log_mutex);
  g_log_level = level;
}

void set_log_info_to_stderr(bool enabled) {
  std::lock_guard lock(g_log_mutex);
  g_log_info_to_stderr = enabled;
}

void Log(LogLevel level, std::string_view message) {
  std::lock_guard lock(g_log_mutex);
  if (static_cast<int>(level) < static_cast<int>(g_log_level)) {
    return;
  }
  const bool to_err = g_log_info_to_stderr || level == LogLevel::Error || level == LogLevel::Warn;
  auto& stream = to_err ? std::cerr : std::cout;
  stream << '[' << LevelName(level) << "] " << message << '\n';
}

}  // namespace engine
