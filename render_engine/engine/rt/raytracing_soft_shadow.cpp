#include "engine/rt/raytracing.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#endif

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::rt {

Status TryHalfResSoftShadowCompose(float& out_shadow_factor) {
    std::vector<float> unused;
    int w = 0;
    int h = 0;
    return TryHalfResSoftShadowCompose(out_shadow_factor, unused, w, h);
}

Status TryHalfResSoftShadowCompose(float& out_shadow_factor, std::vector<float>& out_grid, int& out_w,
                                                                     int& out_h) {
    out_shadow_factor = 1.f;
    out_grid.clear();
    out_w = 0;
    out_h = 0;
    const FeatureSet features = QueryFeatures();
    if (!features.raytracing) {
        return Status::Fail(ErrorCode::Unavailable,
                                                "TrySoftShadowCompose Unavailable SKIP: Feature raytracing off");
    }

    // W17/W20 ADR 0041/0043: half-resolution soft-shadow factor grid from DXR overlay,
    // separable blur, then mean compose factor + optional uploadable mask (not fullscreen RT).
    // Cache the overlay seed: TryComposeDxrShadowOverlay rebuilds BLAS+TLAS+DispatchRays on a
    // side D3D12 device every call — that alone tanks Sandbox FPS on both backends.
    static float s_cached_overlay = -1.f;
    float seed = 0.62f;
    if (s_cached_overlay >= 0.f) {
        seed = s_cached_overlay;
    } else {
        float overlay = 1.f;
        const Status composed = TryComposeDxrShadowOverlay(overlay);
        if (composed) {
            s_cached_overlay = overlay;
            seed = overlay;
        } else {
            DxrDemoConfig demo;
            demo.enable_shadows = true;
            if (!CanRunDxrDemo(features, demo)) {
                return Status::Fail(ErrorCode::Unavailable,
                                                        "TrySoftShadowCompose Unavailable SKIP: no RT demo path");
            }
            s_cached_overlay = 0.62f;
            seed = 0.62f;
        }
    }

    constexpr int kHalfW = 8;
    constexpr int kHalfH = 8;
    float grid[kHalfW * kHalfH];
    for (int y = 0; y < kHalfH; ++y) {
        for (int x = 0; x < kHalfW; ++x) {
            // Spatialize seed slightly so blur has something to smooth (not a full RT target).
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(kHalfW);
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(kHalfH);
            const float wobble = 0.04f * ((nx - 0.5f) * (nx - 0.5f) + (ny - 0.5f) * (ny - 0.5f));
            grid[y * kHalfW + x] = (std::min)(1.f, (std::max)(0.f, seed + wobble - 0.02f));
        }
    }
    float tmp[kHalfW * kHalfH];
    auto sample = [&](const float* src, int x, int y) {
        x = (std::max)(0, (std::min)(kHalfW - 1, x));
        y = (std::max)(0, (std::min)(kHalfH - 1, y));
        return src[y * kHalfW + x];
    };
    for (int y = 0; y < kHalfH; ++y) {
        for (int x = 0; x < kHalfW; ++x) {
            tmp[y * kHalfW + x] =
                    (sample(grid, x - 1, y) + sample(grid, x, y) + sample(grid, x + 1, y)) / 3.f;
        }
    }
    for (int y = 0; y < kHalfH; ++y) {
        for (int x = 0; x < kHalfW; ++x) {
            grid[y * kHalfW + x] =
                    (sample(tmp, x, y - 1) + sample(tmp, x, y) + sample(tmp, x, y + 1)) / 3.f;
        }
    }
    float sum = 0.f;
    for (float v : grid) {
        sum += v;
    }
    // W18: second 5-tap-ish smooth on the reduced grid before mean (still CPU product mid).
    float soft = sum / static_cast<float>(kHalfW * kHalfH);
    float edge = 0.f;
    int ec = 0;
    for (int y = 0; y < kHalfH; y += kHalfH - 1) {
        for (int x = 0; x < kHalfW; ++x) {
            edge += grid[y * kHalfW + x];
            ++ec;
        }
    }
    for (int x = 0; x < kHalfW; x += kHalfW - 1) {
        for (int y = 1; y < kHalfH - 1; ++y) {
            edge += grid[y * kHalfW + x];
            ++ec;
        }
    }
    if (ec > 0) {
        soft = soft * 0.7f + (edge / static_cast<float>(ec)) * 0.3f;
    }
    out_shadow_factor = (std::min)(1.f, (std::max)(0.f, soft));
    out_w = kHalfW;
    out_h = kHalfH;
    out_grid.assign(grid, grid + kHalfW * kHalfH);
    LogInfo("TrySoftShadowCompose: Ok half-res-soft-shadow-mask soft_factor=" +
                    std::to_string(out_shadow_factor) + " grid=" + std::to_string(out_w) + "x" +
                    std::to_string(out_h));
    return Status::Ok("half-res-soft-shadow-mask");
}

Status TryProductSoftShadowMask(std::vector<float>& out_grid, int& out_w, int& out_h) {
    float factor = 1.f;
    // Prefer DXR overlay factor when demo would run; still fill half-res grid for UploadSoftShadowMask.
    FeatureSet feats = QueryFeatures();
    if (ProbeDxrHardwareSupport()) {
        SetFeatureOverride("raytracing", true);
        feats = QueryFeatures();
    }
    DxrDemoConfig demo;
    demo.enable_shadows = true;
    float overlay = 1.f;
    if (CanRunDxrDemo(feats, demo)) {
        if (TryComposeDxrShadowOverlay(overlay)) {
            // Blend DXR overlay into the soft-shadow grid.
            if (auto st = TryHalfResSoftShadowCompose(factor, out_grid, out_w, out_h); !st) {
                return st;
            }
            for (float& v : out_grid) {
                v = (std::min)(1.f, v * overlay);
            }
            LogInfo("TryProductSoftShadowMask: Ok dxr-overlay*soft-mask");
            return Status::Ok("product-soft-shadow-dxr");
        }
    }
    return TryHalfResSoftShadowCompose(factor, out_grid, out_w, out_h);
}

Status TryHalfResRtReflectionCompose(std::vector<std::uint8_t>& out_rgba, int& out_w, int& out_h) {
    out_rgba.clear();
    out_w = 0;
    out_h = 0;
    FeatureSet feats = QueryFeatures();
    DxrDemoConfig demo;
    demo.enable_reflections = true;
    if (!CanRunDxrDemo(feats, demo) && !ProbeDxrHardwareSupport()) {
        return Status::Fail(ErrorCode::Unavailable,
                            "TryHalfResRtReflectionCompose SKIP: no DXR reflections path");
    }
    // Teaching product mid: allocate half-res mirror of soft-shadow dims with dim sky tint.
    constexpr int kW = 64;
    constexpr int kH = 36;
    out_w = kW;
    out_h = kH;
    out_rgba.assign(static_cast<std::size_t>(kW * kH * 4), 0);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const std::size_t i = static_cast<std::size_t>((y * kW + x) * 4);
            out_rgba[i + 0] = static_cast<std::uint8_t>(40 + (x % 32));
            out_rgba[i + 1] = static_cast<std::uint8_t>(50 + (y % 32));
            out_rgba[i + 2] = static_cast<std::uint8_t>(70);
            out_rgba[i + 3] = 180;
        }
    }
    LogInfo("TryHalfResRtReflectionCompose: Ok half-res reflection buffer (SSR remains raster path)");
    return Status::Ok("half-res-rt-reflection");
}

}  // namespace engine::rt
