#include "engine/assets/shader_compile_hook.h"

#include "engine/core/log.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace engine::assets {
namespace {

bool ProbeDxcExecutable() {
#if defined(_WIN32)
  char buf[MAX_PATH]{};
  const DWORD n = SearchPathA(nullptr, "dxc.exe", nullptr, MAX_PATH, buf, nullptr);
  if (n > 0 && n < MAX_PATH) {
    return true;
  }
  const DWORD n2 = SearchPathA(nullptr, "dxc", nullptr, MAX_PATH, buf, nullptr);
  return n2 > 0 && n2 < MAX_PATH;
#else
  // POSIX: rely on `command -v` via system is heavy; check common PATH split.
  if (const char* path = std::getenv("PATH")) {
    std::string p(path);
    std::size_t start = 0;
    while (start <= p.size()) {
      const std::size_t end = p.find(':', start);
      const std::string dir =
          p.substr(start, end == std::string::npos ? std::string::npos : end - start);
      const std::filesystem::path cand = std::filesystem::path(dir) / "dxc";
      std::error_code ec;
      if (!dir.empty() && std::filesystem::exists(cand, ec)) {
        return true;
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
  }
  return false;
#endif
}

}  // namespace

bool IsDxcOnPath() { return ProbeDxcExecutable(); }

Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path) {
  if (!IsDxcOnPath()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryCompileHlslWithDxc Unavailable SKIP: dxc.exe not on PATH");
  }
  std::error_code ec;
  if (hlsl_path.empty() || !std::filesystem::exists(hlsl_path, ec)) {
    return Status::Fail(ErrorCode::NotFound, "TryCompileHlslWithDxc: HLSL path missing");
  }

  const std::string stem = hlsl_path.stem().string();
  const bool looks_vs = stem.find("vs") != std::string::npos ||
                        stem.find("VS") != std::string::npos ||
                        stem.find("vert") != std::string::npos;
  const char* profile = looks_vs ? "vs_6_0" : "ps_6_0";

#if defined(_WIN32)
  // Emit to NUL; we only care that dxc accepts the file.
  const std::string cmd = std::string("dxc.exe -T ") + profile + " -E main \"" +
                          hlsl_path.string() + "\" -Fo NUL 2>NUL";
  const int code = std::system(cmd.c_str());
#else
  const std::string cmd = std::string("dxc -T ") + profile + " -E main \"" + hlsl_path.string() +
                          "\" -Fo /dev/null 2>/dev/null";
  const int code = std::system(cmd.c_str());
#endif
  if (code != 0) {
    LogInfo("TryCompileHlslWithDxc: dxc exit=" + std::to_string(code) + " path=" +
            hlsl_path.string());
    return Status::Fail(ErrorCode::Failed, "TryCompileHlslWithDxc: dxc compile failed");
  }
  return Status::Ok("dxc-compile");
}

}  // namespace engine::assets
