#include "engine/app/application.h"

#include "engine/core/clock.h"
#include "engine/core/log.h"
#include "engine/net/net_system.h"

#include <cmath>

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
  input_.set_mouse_wheel(snap.mouse_wheel);
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

    const float sprint =
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 2.4f : 1.f;
    const float move_speed = move_speed_ * sprint * dt_;
    const auto& snap = window_->input_snapshot();
    const bool lmb = snap.mouse_left;
    const bool rmb = snap.mouse_right;
    const bool mmb = snap.mouse_middle;
    const bool want_look =
        !ui_want_capture_ && ((look_with_lmb_ && lmb) || (look_with_rmb_ && rmb));
    const bool want_pan = !ui_want_capture_ && mmb;

    if (want_look || want_pan) {
      window_->SetCursorCaptured(true);
      if (hide_cursor_on_look_ && want_look) {
        window_->SetCursorLocked(true);
      }
    } else {
      if (was_looking_) {
        window_->SetCursorLocked(false);
        window_->SetCursorCaptured(false);
      }
    }
    was_looking_ = want_look || want_pan;

    if (!ui_want_capture_) {
      camera_.MoveLocal(input_.axis("MoveZ") * move_speed, input_.axis("MoveX") * move_speed,
                        input_.axis("MoveY") * move_speed);
      if (want_pan) {
        // Middle-drag pans in view plane (screen X → right, screen Y → up).
        camera_.MoveLocal(0.f, -input_.axis("LookX") * pan_sensitivity_,
                          input_.axis("LookY") * pan_sensitivity_);
      } else if (want_look) {
        camera_.AddYawPitch(-input_.axis("LookX") * look_sensitivity_,
                            -input_.axis("LookY") * look_sensitivity_);
      }
      if (std::fabs(input_.mouse_wheel()) > 1e-6f) {
        // Wheel dolly along look direction (zoom).
        camera_.MoveLocal(input_.mouse_wheel() * zoom_sensitivity_, 0.f, 0.f);
      }
    } else {
      window_->SetCursorLocked(false);
      window_->SetCursorCaptured(false);
      was_looking_ = false;
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

    window_->ConsumeMouseWheel();
    input_.EndFrame();
    debug_draw_.Clear();

    if (headless_frames_ > 0 && frame_index_ >= headless_frames_) {
      window_->RequestClose();
    }
  }

  modules_.ShutdownAll(*this);
  LogInfo("Application loop end");
  return Status::Ok();
}

}  // namespace engine
