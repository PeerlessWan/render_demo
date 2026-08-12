#include "engine/app/application.h"

#include "engine/core/clock.h"
#include "engine/core/log.h"
#include "engine/net/net_system.h"

namespace engine {

Application::Application(std::unique_ptr<Window> window, std::unique_ptr<rhi::IDevice> device,
                         ColorRgba clear_color)
    : window_(std::move(window)), device_(std::move(device)), clear_color_(clear_color) {
  input_.InstallFlyCameraDefaults();
}

Result<std::unique_ptr<Application>> Application::Create(const ApplicationDesc& desc) {
  auto window = Window::Create(desc.window);
  if (!window) {
    return Result<std::unique_ptr<Application>>::Fail(window.status());
  }

  rhi::DeviceDesc device_desc{};
  device_desc.native_window = window.value()->native_handle();
  device_desc.width = window.value()->width();
  device_desc.height = window.value()->height();

  auto device = rhi::CreateD3D12Device(device_desc);
  if (!device) {
    return Result<std::unique_ptr<Application>>::Fail(device.status());
  }

  auto app = std::unique_ptr<Application>(
      new Application(std::move(window.value()), std::move(device.value()), desc.clear_color));
  return Result<std::unique_ptr<Application>>::Ok(std::move(app));
}

Status Application::Run(FrameCallback on_frame) {
  LogInfo("Application loop start");
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

    // --- Input ---
    input_.BeginFrame();
    window_->PumpEvents();
    if (window_->should_close()) {
      break;
    }
    input_.EvaluateActions();

    // Fly camera from actions (M4 acceptance).
    const float move_speed = 4.f * dt_;
    const float look_speed = 0.0025f;
    camera_.MoveLocal(input_.axis("MoveZ") * move_speed, input_.axis("MoveX") * move_speed,
                      input_.axis("MoveY") * move_speed);
    camera_.AddYawPitch(-input_.axis("LookX") * look_speed, -input_.axis("LookY") * look_speed);

    // --- Net.Pump ---
    if (net_) {
      net_->Pump();
    }

    // --- Asset.PumpAsync ---
    assets_.PumpAsync();

    // --- Module.OnUpdate ---
    modules_.UpdateAll(*this, dt_);

    // --- World + Extract (render never writes World) ---
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
  }

  modules_.ShutdownAll(*this);
  LogInfo("Application loop end");
  return Status::Ok();
}

}  // namespace engine
