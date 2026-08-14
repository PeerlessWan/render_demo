#include "engine/core/log.h"
#include "engine/core/math.h"
#include "engine/platform/window.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#ifndef ENGINE_SHADER_DIR_A
#error "ENGINE_SHADER_DIR_A must be set by CMake"
#endif

namespace {

void ParseArgs(int argc, char** argv, bool& headless, int& headless_frames) {
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
}

}  // namespace

int main(int argc, char** argv) {
  bool headless = false;
  int headless_frames = 0;
  ParseArgs(argc, argv, headless, headless_frames);

  engine::WindowDesc wdesc{};
  wdesc.title = "Learn 06 — RHI Triangle";
  wdesc.width = 1280;
  wdesc.height = 720;
  wdesc.headless = headless;

  auto window = engine::Window::Create(wdesc);
  if (!window) {
    engine::LogError(window.status().message());
    return 1;
  }

  engine::rhi::DeviceDesc ddesc{};
  ddesc.native_window = window.value()->native_handle();
  ddesc.width = window.value()->width();
  ddesc.height = window.value()->height();
  ddesc.headless = headless;

  auto device = engine::rhi::CreateDevice(engine::rhi::Backend::D3D12, ddesc);
  if (!device) {
    engine::LogError(device.status().message());
    return 1;
  }

  const auto shader_dir = std::filesystem::path(ENGINE_SHADER_DIR_A);
  engine::rhi::SimpleMeshShaders shaders;
  shaders.vs_dxil = shader_dir / "triangle.vs.cso";
  shaders.ps_dxil = shader_dir / "triangle.ps.cso";
  if (auto st = device.value()->SetupSimpleMesh(shaders); !st) {
    engine::LogError(st.message());
    return 1;
  }

  const engine::ColorRgba clear{0.05f, 0.07f, 0.1f, 1.f};
  int frame = 0;

  while (!window.value()->should_close()) {
    window.value()->PumpEvents();
    if (headless_frames > 0 && frame >= headless_frames) {
      window.value()->RequestClose();
      break;
    }

    if (auto st = device.value()->BeginFrame(); !st) {
      engine::LogError(st.message());
      return 1;
    }
    if (auto st = device.value()->Clear(clear); !st) {
      engine::LogError(st.message());
      return 1;
    }
    if (auto st = device.value()->DrawSimpleMesh(); !st) {
      engine::LogError(st.message());
      return 1;
    }
    if (auto st = device.value()->Present(); !st) {
      engine::LogError(st.message());
      return 1;
    }
    ++frame;
  }

  return 0;
}
