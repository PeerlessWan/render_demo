#pragma once

#include "engine/core/result.h"
#include "engine/render/camera.h"
#include "engine/render/environment.h"
#include "engine/render/frame_graph.h"
#include "engine/render/render_scene.h"
#include "engine/rhi/i_device.h"

#include <filesystem>

namespace engine::render {

struct RenderSystemDesc {
  std::filesystem::path lit_vs;
  std::filesystem::path lit_ps;
};

// Usable render entry: Extract → FrameGraph (Opaque) → lit cubes.
class RenderSystem {
 public:
  Status Init(rhi::IDevice& device, const RenderSystemDesc& desc);
  Status DrawFrame(rhi::IDevice& device, const RenderScene& scene, const Environment& env,
                   float aspect);

  [[nodiscard]] const FrameGraph& frame_graph() const { return graph_; }
  [[nodiscard]] std::uint32_t last_draw_count() const { return last_draw_count_; }

 private:
  bool ready_ = false;
  FrameGraph graph_;
  std::uint32_t last_draw_count_ = 0;
};

}  // namespace engine::render
