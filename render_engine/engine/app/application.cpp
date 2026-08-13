#include "engine/app/application.h"

#include "engine/core/clock.h"
#include "engine/core/log.h"
#include "engine/net/net_system.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace engine {
namespace {

input::Key MapVk(std::size_t vk) {
  switch (vk) {
    case 'W':
      return input::Key::W;
    case 'A':
      return input::Key::A;
    case 'S':
      return input::Key::S;
    case 'D':
      return input::Key::D;
    case 'Q':
      return input::Key::Q;
    case 'E':
      return input::Key::E;
    case VK_SPACE:
      return input::Key::Space;
    case VK_ESCAPE:
      return input::Key::Escape;
    default:
      return input::Key::Unknown;
  }
}

}  // namespace

Application::Application(std::unique_ptr<Window> window, std::unique_ptr<rhi::IDevice> device,
                         ColorRgba clear_color, bool headless, int headless_frames)
    : window_(std::move(window)),
      device_(std::move(device)),
      clear_color_(clear_color),
      headless_(headless),
      headless_frames_(headless_frames) {
  input_.InstallFlyCameraDefaults();
}

void Application::SyncInputFromWindow() {
  const auto& snap = window_->input_snapshot();
  for (std::size_t vk = 0; vk < snap.keys.size(); ++vk) {
    const auto key = MapVk(vk);
    if (key != input::Key::Unknown) {
      input_.set_key(key, snap.keys[vk]);
    }
  }
  input_.set_mouse_delta(snap.mouse_dx, snap.mouse_dy);
  window_->ConsumeMouseDelta();
}

Result<std::unique_ptr<Application>> Application::Create(const ApplicationDesc& desc) {
  WindowDesc wdesc = desc.window;
  wdesc.headless = desc.headless || desc.window.headless;

  auto window = Window::Create(wdesc);
  if (!window) {
    return Result<std::unique_ptr<Application>>::Fail(window.status());
  }

  rhi::DeviceDesc device_desc{};
  device_desc.native_window = window.value()->native_handle();
  device_desc.width = window.value()->width();
  device_desc.height = window.value()->height();
  device_desc.headless = window.value()->is_headless();

  // Headless always resolved inside CreateDevice (both D3D12 and Vulkan).
  Result<std::unique_ptr<rhi::IDevice>> device = rhi::CreateDevice(desc.backend, device_desc);
  if (!device) {
    return Result<std::unique_ptr<Application>>::Fail(device.status());
  }

  auto app = std::unique_ptr<Application>(
      new Application(std::move(window.value()), std::move(device.value()), desc.clear_color,
                      device_desc.headless, desc.headless_frames));
  return Result<std::unique_ptr<Application>>::Ok(std::move(app));
}

Status Application::Run(FrameCallback on_frame) {
  LogInfo(headless_ ? "Application loop start (headless)" : "Application loop start");
  if (auto st = modules_.InitAll(*this); !st) {
    LogError(st.message());
    return st;
  }

  std::uint32_t last_w = window_->width();
  std::uint32_t last_h = window_->height();
  Clock clock;

  while (!window_->should_close()) {
    dt_ = static_cast<float>(clock.Tick());
    if (dt_ <= 0.f) {
      dt_ = 1.f / 60.f;
    }
    ++frame_index_;

    input_.BeginFrame();
    window_->PumpEvents();
    if (window_->should_close()) {
      break;
    }
    SyncInputFromWindow();
    input_.EvaluateActions();

    if (input_.key_down(input::Key::Escape)) {
      window_->RequestClose();
    }

    const float move_speed = 4.f * dt_;
    const float look_speed = 0.0025f;
    if (!ui_want_capture_) {
      camera_.MoveLocal(input_.axis("MoveZ") * move_speed, input_.axis("MoveX") * move_speed,
                        input_.axis("MoveY") * move_speed);
      camera_.AddYawPitch(-input_.axis("LookX") * look_speed, -input_.axis("LookY") * look_speed);
    } else {
      // Still allow WASD while hovering UI panel? Block all for predictable tuning.
    }

    if (net_) {
      net_->Pump();
    }
    assets_.PumpAsync();
    modules_.UpdateAll(*this, dt_);

    world_.UpdateTransforms();
    const float aspect = last_h > 0 ? static_cast<float>(last_w) / static_cast<float>(last_h) : 1.f;
    render_scene_ = render::RenderSceneExtractor::Extract(world_, camera_, aspect);

    if (window_->width() != last_w || window_->height() != last_h) {
      last_w = window_->width();
      last_h = window_->height();
      if (auto st = device_->Resize(last_w, last_h); !st) {
        LogError(st.message());
        return st;
      }
    }

    if (auto st = device_->BeginFrame(); !st) {
      LogError(st.message());
      return st;
    }
    if (auto st = device_->Clear(clear_color_); !st) {
      LogError(st.message());
      return st;
    }
    if (on_frame) {
      on_frame(*this);
    }
    if (auto st = device_->Present(); !st) {
      LogError(st.message());
      return st;
    }

    input_.EndFrame();
    debug_draw_.Clear();

    if (headless_ && headless_frames_ > 0 && frame_index_ >= headless_frames_) {
      window_->RequestClose();
    }
  }

  modules_.ShutdownAll(*this);
  LogInfo("Application loop end");
  return Status::Ok();
}

}  // namespace engine
