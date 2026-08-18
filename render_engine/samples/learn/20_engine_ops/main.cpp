#include "engine/assets/asset_system.h"
#include "engine/assets/manifest.h"
#include "engine/core/log.h"
#include "engine/debug/console.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

void ParseHeadless(int argc, char** argv, int& headless_frames) {
  bool headless = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      headless = true;
      if (headless_frames <= 0) {
        headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      headless_frames = std::atoi(argv[++i]);
    }
  }
  if (headless && headless_frames <= 0) {
    headless_frames = 2;
  }
}

}  // namespace

int main(int argc, char** argv) {
  int headless_frames = 0;
  ParseHeadless(argc, argv, headless_frames);

  engine::debug::Console console;
  console.Register("echo", [](const std::vector<std::string>& args) {
    std::string line = "echo:";
    for (const auto& a : args) {
      line += " ";
      line += a;
    }
    engine::LogInfo(line);
    return engine::Status::Ok();
  });
  (void)console.Execute("echo learn 20_engine_ops");

  engine::assets::AssetSystem assets;
  engine::assets::Manifest manifest;
  // Empty manifest: RequestLoad will complete with failure or missing — Pump still drains.
  if (auto st = assets.SetManifest(manifest); !st) {
    engine::LogError(st.message());
    return 1;
  }

  bool callback_fired = false;
  engine::assets::AssetId id{"learn/missing_placeholder.bin"};
  auto handle = assets.RequestLoad(id, [&](engine::Status status, engine::assets::AssetHandle h) {
    callback_fired = true;
    engine::LogInfo(std::string("LoadCallback ok=") + (status ? "true" : "false") +
                    " handle_alive=" + (h ? "true" : "false") +
                    (status ? "" : (" msg=" + status.message())));
  });
  engine::LogInfo(std::string("RequestLoad issued handle_alive=") + (handle ? "true" : "false"));

  for (int i = 0; i < 32 && !callback_fired; ++i) {
    assets.PumpAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  assets.PumpAsync();
  engine::LogInfo(std::string("PumpAsync done callback_fired=") +
                  (callback_fired ? "true" : "false"));
  engine::LogInfo("Handle lifetime: keep AssetHandle until GPU Fence / consumers done "
                  "(see RUNTIME_FOUNDATIONS)");

  (void)headless_frames;
  return 0;
}
