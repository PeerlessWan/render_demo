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

bool ProbeVkRtHardwareSupport() {
#if ENGINE_WITH_VULKAN
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "engine_vk_rt_probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
        return false;
    }
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
    bool found = false;
    if (phys_count > 0) {
        std::vector<VkPhysicalDevice> phys_list(phys_count);
        vkEnumeratePhysicalDevices(instance, &phys_count, phys_list.data());
        for (VkPhysicalDevice pd : phys_list) {
            uint32_t ext_count = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
            for (const auto& e : exts) {
                if (std::strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0 ||
                        std::strcmp(e.extensionName, "VK_KHR_ray_query") == 0) {
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
    }
    vkDestroyInstance(instance, nullptr);
    return found;
#else
    return false;
#endif
}

bool CanRunProductRtPath() {
    return ProbeDxrHardwareSupport() || ProbeVkRtHardwareSupport();
}

}  // namespace engine::rt
