#pragma once

#include "engine/app/module.h"
#include "engine/assets/asset_system.h"
#include "engine/core/math.h"
#include "engine/core/result.h"
#include "engine/debug/debug_draw.h"
#include "engine/input/input_system.h"
#include "engine/platform/window.h"
#include "engine/render/camera.h"
#include "engine/render/render_scene.h"
#include "engine/rhi/backend.h"
#include "engine/rhi/i_device.h"
#include "engine/scene/world.h"

#include <functional>
#include <memory>

namespace engine::net {
class NetSystem;
}

namespace engine {

struct ApplicationDesc {
  WindowDesc window{};
  ColorRgba clear_color{0.1f, 0.2f, 0.35f, 1.f};
  rhi::Backend backend = rhi::Backend::D3D12;  // Sandbox/default; set Vulkan for M17 clear
  bool headless = false;
  int headless_frames = 0;  // >0: auto RequestClose after N frames (headless CI)
};

class Application {
 public:
  using FrameCallback = std::function<void(Application&)>;

  static Result<std::unique_ptr<Application>> Create(const ApplicationDesc& desc);

  Window& window() { return *window_; }
  const Window& window() const { return *window_; }
  rhi::IDevice& device() { return *device_; }
  const rhi::IDevice& device() const { return *device_; }

  scene::World& world() { return world_; }
  const scene::World& world() const { return world_; }
  render::Camera& camera() { return camera_; }
  const render::Camera& camera() const { return camera_; }
  const render::RenderScene& render_scene() const { return render_scene_; }
  input::InputSystem& input() { return input_; }
  assets::AssetSystem& assets() { return assets_; }
  ModuleSystem& modules() { return modules_; }
  debug::DebugDraw& debug_draw() { return debug_draw_; }

  void set_net(std::shared_ptr<net::NetSystem> net) { net_ = std::move(net); }
  net::NetSystem* net() { return net_.get(); }

  // When true, Application skips camera look (set by UI WantCapture, typically previous frame).
  void set_ui_want_capture(bool v) { ui_want_capture_ = v; }
  [[nodiscard]] bool ui_want_capture() const { return ui_want_capture_; }

  [[nodiscard]] const ColorRgba& clear_color() const { return clear_color_; }
  void set_clear_color(const ColorRgba& color) { clear_color_ = color; }
  [[nodiscard]] float delta_time() const { return dt_; }
  [[nodiscard]] bool is_headless() const { return headless_; }
  [[nodiscard]] int frame_index() const { return frame_index_; }

  Status Run(FrameCallback on_frame = {});

 private:
  Application(std::unique_ptr<Window> window, std::unique_ptr<rhi::IDevice> device,
              ColorRgba clear_color, bool headless, int headless_frames);

  void SyncInputFromWindow();

  std::unique_ptr<Window> window_;
  std::unique_ptr<rhi::IDevice> device_;
  ColorRgba clear_color_;
  float dt_ = 0.f;
  bool headless_ = false;
  int headless_frames_ = 0;
  int frame_index_ = 0;

  scene::World world_;
  render::Camera camera_;
  render::RenderScene render_scene_;
  input::InputSystem input_;
  assets::AssetSystem assets_;
  ModuleSystem modules_;
  debug::DebugDraw debug_draw_;
  std::shared_ptr<net::NetSystem> net_;
  bool ui_want_capture_ = false;
};

}  // namespace engine
