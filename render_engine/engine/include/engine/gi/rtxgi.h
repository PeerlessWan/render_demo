#pragma once

#include "engine/core/math.h"
#include "engine/core/result.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace engine::gi {

// W23 / ADR 0046: optional NVIDIA RTXGI (true DDGI). No SDK → honest SKIP.
// Product default remains CascadeGi. Never rename CascadeGi as DDGI.

enum class GiGpuApi : int { None = 0, D3D12 = 1, Vulkan = 2 };

void BindGiGpuDevice(GiGpuApi api, void* native_device_or_null);
[[nodiscard]] bool GiGpuDeviceBound();
[[nodiscard]] GiGpuApi GiBoundApi();

struct RtxgiVolumeDesc {
  Vec3 origin{0, 0, 0};
  Vec3 spacing{1, 1, 1};
  int nx = 8;
  int ny = 4;
  int nz = 8;
};

class IRtxgiVolume {
 public:
  virtual ~IRtxgiVolume() = default;
  [[nodiscard]] virtual const char* name() const = 0;
  [[nodiscard]] virtual bool ready() const = 0;
  // Update probe irradiance; fills RGBA8 atlas (nx*ny*nz texels) when ready.
  virtual Status Update(std::vector<std::uint8_t>& out_rgba8_atlas, int& out_w, int& out_h) = 0;
};

// Returns nullptr when ENGINE_WITH_RTXGI off, headers missing, or evaluate not linked.
[[nodiscard]] std::unique_ptr<IRtxgiVolume> TryCreateRtxgiVolume(const RtxgiVolumeDesc& desc);

}  // namespace engine::gi
