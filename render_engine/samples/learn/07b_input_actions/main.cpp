#include "engine/app/application.h"
#include "engine/core/log.h"
#include "engine/input/input_system.h"

#include <cstdlib>
#include <string>

namespace {

void ParseHeadless(int argc, char** argv, engine::ApplicationDesc& desc) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i] ? argv[i] : "";
    if (arg == "--headless") {
      desc.headless = true;
      desc.window.headless = true;
      if (desc.headless_frames <= 0) {
        desc.headless_frames = 2;
      }
    } else if (arg.rfind("--headless_frames=", 0) == 0) {
      desc.headless_frames = std::atoi(arg.c_str() + 18);
    } else if (arg == "--headless_frames" && i + 1 < argc) {
      desc.headless_frames = std::atoi(argv[++i]);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  engine::ApplicationDesc desc{};
  desc.window.title = "Learn 07b — Input Actions";
  ParseHeadless(argc, argv, desc);

  auto app = engine::Application::Create(desc);
  if (!app) {
    engine::LogError(app.status().message());
    return 1;
  }

  auto& a = *app.value();
  a.input().action_map().Bind("Fire", "Button:Space");
  a.input().action_map().Bind("Sprint", "Button:KeyShift");
  engine::LogInfo("ActionMap: Fire=Space, Sprint=Shift (interactive mode only)");

  const auto status = a.Run([&](engine::Application& app_ref) {
    if (app_ref.is_headless()) {
      return;
    }
    if (app_ref.input().pressed("Fire")) {
      engine::LogInfo("Action Fire pressed");
    }
    if (app_ref.input().key_down(engine::input::Key::W)) {
      engine::LogInfo("Move forward (W held)");
    }
  });
  return status ? 0 : 1;
}
