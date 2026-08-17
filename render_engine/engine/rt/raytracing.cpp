#include "engine/rt/raytracing.h"

#include "engine/core/log.h"

#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

namespace engine::rt {

RtStatus Resolve(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  if (!cfg.enable) {
    return RtStatus::Disabled;
  }
  if (backend == rhi::Backend::D3D12 && features.raytracing) {
    return RtStatus::Supported;
  }
  if (backend == rhi::Backend::Vulkan && features.raytracing) {
    return RtStatus::Supported;
  }
  if (cfg.allow_fallback) {
    return RtStatus::UnsupportedFallback;
  }
  return RtStatus::Unavailable;
}

Status EnsureSafe(rhi::Backend backend, const FeatureSet& features, const RaytracingConfig& cfg) {
  const auto st = Resolve(backend, features, cfg);
  if (st == RtStatus::Unavailable) {
    return Status::Fail(ErrorCode::Unavailable, "raytracing requested but unsupported");
  }
  return Status::Ok();
}

bool ProbeDxrHardwareSupport() {
#if defined(_WIN32)
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return false;
  }
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        !device) {
      continue;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) &&
        opts5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      return true;
    }
  }
  return false;
#else
  return false;
#endif
}

bool CanRunDxrDemo(const FeatureSet& features, const DxrDemoConfig& demo) {
  if (!features.raytracing || !features.d3d12) {
    return false;
  }
  return demo.enable_reflections || demo.enable_shadows;
}

DxrShadowDemoResult DxrShadowDemo(const FeatureSet& features, const DxrDemoConfig& demo) {
  DxrShadowDemoResult out;
  // W4 deepen: record intent only — no AS/SBT/DispatchRays.
  out.would_run = features.raytracing && features.d3d12 && demo.enable_shadows;
  if (out.would_run) {
    LogInfo("DxrShadowDemo: shadow demo pass WOULD run (stub dispatch contract, ADR 0030)");
  }
  return out;
}

Status RunDxrFullscreenStub(rhi::IDevice& device) {
  (void)device;
  const FeatureSet features = QueryFeatures();
  if (!features.raytracing) {
    return Status::Fail(ErrorCode::Unavailable,
                        "RunDxrFullscreenStub unavailable (raytracing feature false)");
  }
  // Prefer validating DXR headers via empty-TLAS prebuild when possible; soft-skip if not.
  const Status tlas = TryEmptyTlasPrebuild();
  if (!tlas && tlas.code() != ErrorCode::Unavailable) {
    return tlas;
  }
  LogInfo("RunDxrFullscreenStub: fullscreen demo pass WOULD DispatchRays "
          "(W4 stub contract; full DispatchRays deferred — ADR 0030)");
  return Status::Ok();
}

Status TryEmptyTlasPrebuild() {
#if defined(_WIN32)
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory4> factory;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory) {
    return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: DXGI factory failed");
  }
  for (UINT i = 0;; ++i) {
    ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (FAILED(adapter->GetDesc1(&desc))) {
      continue;
    }
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      continue;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        !device) {
      continue;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5{};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &opts5, sizeof(opts5))) ||
        opts5.RaytracingTier == D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
      continue;
    }
    ComPtr<ID3D12Device5> device5;
    if (FAILED(device.As(&device5)) || !device5) {
      return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: ID3D12Device5 missing");
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 0;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = 0;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    LogInfo("TryEmptyTlasPrebuild: empty TLAS prebuild ResultDataMaxSizeInBytes=" +
            std::to_string(info.ResultDataMaxSizeInBytes));
    return Status::Ok();
  }
  return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: no DXR-capable adapter");
#else
  return Status::Fail(ErrorCode::Unavailable, "TryEmptyTlasPrebuild: DXR requires Windows/D3D12");
#endif
}

}  // namespace engine::rt
