#pragma once

#include <string_view>

namespace engine {

enum class LogLevel {
  Trace,
  Info,
  Warn,
  Error,
};

void set_log_level(LogLevel level);
void Log(LogLevel level, std::string_view message);

inline void LogInfo(std::string_view message) { Log(LogLevel::Info, message); }
inline void LogWarn(std::string_view message) { Log(LogLevel::Warn, message); }
inline void LogError(std::string_view message) { Log(LogLevel::Error, message); }

}  // namespace engine
