#include "engine/assets/shader_compile_hook.h"

#include "engine/core/log.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
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

void InferProfileEntry(const std::filesystem::path& hlsl_path, std::string& profile,
                       std::string& entry) {
  const std::string stem = hlsl_path.stem().string();
  const std::string name = hlsl_path.filename().string();
  profile = "ps_6_0";
  entry = "PSMain";
  if (stem.find("vs") != std::string::npos || stem.find("VS") != std::string::npos ||
      stem.find("vert") != std::string::npos) {
    profile = "vs_6_0";
    entry = "VSMain";
  } else if (stem.find("cs") != std::string::npos || stem.find("CS") != std::string::npos ||
             name.find("_cs") != std::string::npos) {
    profile = "cs_6_0";
    entry = "CSMain";
  } else if (stem.find("ms") != std::string::npos || name.find("_ms") != std::string::npos) {
    profile = "ms_6_5";
    entry = "MSMain";
  } else if (stem.find("lit_cube") != std::string::npos || stem.find("lit_") != std::string::npos) {
    // Default lit pair: callers often compile PS; VS via explicit entry overload.
    profile = "ps_6_0";
    entry = "PSMain";
  }
}

}  // namespace

bool IsDxcOnPath() { return ProbeDxcExecutable(); }

Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path) {
  return TryCompileHlslWithDxc(hlsl_path, {}, {}, {});
}

Status TryCompileHlslWithDxc(const std::filesystem::path& hlsl_path,
                             const std::filesystem::path& out_cso, std::string_view entry,
                             std::string_view profile) {
  if (!IsDxcOnPath()) {
    return Status::Fail(ErrorCode::Unavailable,
                        "TryCompileHlslWithDxc Unavailable SKIP: dxc.exe not on PATH");
  }
  std::error_code ec;
  if (hlsl_path.empty() || !std::filesystem::exists(hlsl_path, ec)) {
    return Status::Fail(ErrorCode::NotFound, "TryCompileHlslWithDxc: HLSL path missing");
  }

  std::string use_profile;
  std::string use_entry;
  if (!profile.empty() && !entry.empty()) {
    use_profile = std::string(profile);
    use_entry = std::string(entry);
  } else {
    InferProfileEntry(hlsl_path, use_profile, use_entry);
    if (!profile.empty()) {
      use_profile = std::string(profile);
    }
    if (!entry.empty()) {
      use_entry = std::string(entry);
    }
  }

  std::filesystem::path fo = out_cso;
  if (fo.empty()) {
#if defined(_WIN32)
    fo = "NUL";
#else
    fo = "/dev/null";
#endif
  } else if (fo.has_parent_path()) {
    std::filesystem::create_directories(fo.parent_path(), ec);
  }

#if defined(_WIN32)
  const std::string cmd = std::string("dxc.exe -T ") + use_profile + " -E " + use_entry + " \"" +
                          hlsl_path.string() + "\" -Fo \"" + fo.string() + "\" 2>NUL";
  const int code = std::system(cmd.c_str());
#else
  const std::string cmd = std::string("dxc -T ") + use_profile + " -E " + use_entry + " \"" +
                          hlsl_path.string() + "\" -Fo \"" + fo.string() + "\" 2>/dev/null";
  const int code = std::system(cmd.c_str());
#endif
  if (code != 0) {
    LogInfo("TryCompileHlslWithDxc: dxc exit=" + std::to_string(code) + " path=" +
            hlsl_path.string() + " entry=" + use_entry);
    return Status::Fail(ErrorCode::Failed, "TryCompileHlslWithDxc: dxc compile failed");
  }
  return Status::Ok(out_cso.empty() ? "dxc-compile" : "dxc-write-cso");
}

}  // namespace engine::assets
