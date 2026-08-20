#include "engine/gi/rtxgi.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <string>

#if defined(ENGINE_WITH_RTXGI) && ENGINE_WITH_RTXGI
#if defined(__has_include)
#if __has_include(<ddgi/DDGIVolume.h>)
#include <ddgi/DDGIVolume.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include("ddgi/DDGIVolume.h")
#include "ddgi/DDGIVolume.h"
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include(<rtxgi/ddgi/DDGIVolume.h>)
#include <rtxgi/ddgi/DDGIVolume.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include(<RTXGI.h>)
#include <RTXGI.h>
#define ENGINE_HAS_RTXGI_HEADERS 1
#elif __has_include("rtxgi.h")
#include "rtxgi.h"
#define ENGINE_HAS_RTXGI_HEADERS 1
#endif
#endif
#endif

namespace engine::gi {
namespace {

GiGpuApi g_bound_api = GiGpuApi::None;
void* g_bound_device = nullptr;

class RtxgiVolumeStub final : public IRtxgiVolume {
 public:
  explicit RtxgiVolumeStub(RtxgiVolumeDesc d, bool ready) : desc_(d), ready_(ready) {}
  [[nodiscard]] const char* name() const override { return "rtxgi"; }
  [[nodiscard]] bool ready() const override { return ready_; }
  Status Update(std::vector<std::uint8_t>& out_rgba8_atlas, int& out_w, int& out_h) override {
    if (!ready_) {
      return Status::Fail(ErrorCode::Unavailable,
                          "RTXGI Update SKIP: vendor evaluate not linked (ADR 0046)");
    }
    (void)desc_;
    (void)out_rgba8_atlas;
    out_w = 0;
    out_h = 0;
    return Status::Fail(ErrorCode::Unavailable, "RTXGI Update SKIP: evaluate entry not implemented");
  }

 private:
  RtxgiVolumeDesc desc_{};
  bool ready_ = false;
};

}  // namespace

void BindGiGpuDevice(GiGpuApi api, void* native_device_or_null) {
  g_bound_api = api;
  g_bound_device = native_device_or_null;
  if (api != GiGpuApi::None && native_device_or_null) {
    LogInfo("GI GPU device bound for RTXGI (ADR 0046)");
  }
}

bool GiGpuDeviceBound() { return g_bound_api != GiGpuApi::None && g_bound_device != nullptr; }

GiGpuApi GiBoundApi() { return g_bound_api; }

std::unique_ptr<IRtxgiVolume> TryCreateRtxgiVolume(const RtxgiVolumeDesc& desc) {
  const bool bound = GiGpuDeviceBound();
#if defined(ENGINE_HAS_RTXGI_HEADERS)
  SetFeatureOverride("rtxgi", bound);
  if (!bound) {
    return nullptr;
  }
  // Headers present: evaluate not wired → ready=false (honest SKIP on Update).
  return std::make_unique<RtxgiVolumeStub>(desc, /*ready=*/false);
#elif defined(ENGINE_WITH_RTXGI) && ENGINE_WITH_RTXGI
  SetFeatureOverride("rtxgi", false);
  static bool once = false;
  if (!once) {
    once = true;
    LogWarn("TryCreateRtxgiVolume: ENGINE_WITH_RTXGI=1 but RTXGI headers missing → nullptr");
  }
  (void)desc;
  (void)bound;
  return nullptr;
#else
  SetFeatureOverride("rtxgi", false);
  (void)desc;
  (void)bound;
  return nullptr;
#endif
}

}  // namespace engine::gi
