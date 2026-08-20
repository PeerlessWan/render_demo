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

Status TryVkTraceRaysDemoStub() {
#if ENGINE_WITH_VULKAN
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "engine_vk_tracerays_probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
        return Status::Fail(ErrorCode::Unavailable,
                                                "TryVkTraceRaysDemoStub: vkCreateInstance failed SKIP");
    }

    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    uint32_t queue_family = 0;
    bool has_rt_pipeline = false;
    bool has_as = false;
    bool has_deferred = false;
    bool has_bda = false;
    if (phys_count > 0) {
        std::vector<VkPhysicalDevice> phys_list(phys_count);
        vkEnumeratePhysicalDevices(instance, &phys_count, phys_list.data());
        for (VkPhysicalDevice pd : phys_list) {
            uint32_t ext_count = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
            has_rt_pipeline = false;
            has_as = false;
            has_deferred = false;
            has_bda = false;
            for (const auto& e : exts) {
                if (std::strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0) {
                    has_rt_pipeline = true;
                } else if (std::strcmp(e.extensionName,
                                                             VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0) {
                    has_as = true;
                } else if (std::strcmp(e.extensionName,
                                                             VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) == 0) {
                    has_deferred = true;
                } else if (std::strcmp(e.extensionName,
                                                             VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME) == 0) {
                    has_bda = true;
                }
            }
            if (!has_rt_pipeline) {
                continue;
            }
            uint32_t qcount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
            std::vector<VkQueueFamilyProperties> qprops(qcount);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());
            for (uint32_t i = 0; i < qcount; ++i) {
                if (qprops[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                    phys = pd;
                    queue_family = i;
                    break;
                }
            }
            if (phys) {
                break;
            }
        }
    }

    if (!phys || !has_rt_pipeline) {
        vkDestroyInstance(instance, nullptr);
        return Status::Fail(ErrorCode::Unavailable,
                                                "TryVkTraceRaysDemoStub Unavailable SKIP: no VK_KHR_ray_tracing_pipeline");
    }

    // Mega-W11: create a device with RT pipeline features and resolve vkCmdTraceRaysKHR.
    std::vector<const char*> enabled_exts;
    enabled_exts.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    if (has_as) {
        enabled_exts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    }
    if (has_deferred) {
        enabled_exts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
    if (has_bda) {
        enabled_exts.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    }

    VkPhysicalDeviceBufferDeviceAddressFeatures bda_f{};
    bda_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    bda_f.bufferDeviceAddress = has_bda ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_f{};
    as_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    as_f.accelerationStructure = has_as ? VK_TRUE : VK_FALSE;
    as_f.pNext = has_bda ? &bda_f : nullptr;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_f{};
    rt_f.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
    rt_f.rayTracingPipeline = VK_TRUE;
    rt_f.pNext = (has_as || has_bda) ? &as_f : nullptr;

    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &rt_f;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
    dci.ppEnabledExtensionNames = enabled_exts.data();

    VkDevice device = VK_NULL_HANDLE;
    const VkResult cr = vkCreateDevice(phys, &dci, nullptr, &device);
    if (cr != VK_SUCCESS || !device) {
        vkDestroyInstance(instance, nullptr);
        return Status::Fail(ErrorCode::Unavailable,
                                                "TryVkTraceRaysDemoStub Unavailable SKIP: vkCreateDevice RT failed");
    }

    auto fn = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
            vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    if (!fn) {
        return Status::Fail(ErrorCode::Unavailable,
                                                "TryVkTraceRaysDemoStub Unavailable SKIP: vkCmdTraceRaysKHR unresolved");
    }

    LogInfo("TryVkTraceRaysDemoStub: vkCmdTraceRaysKHR resolved (rayTracingPipeline Ok)");
    return Status::Ok("vk-tracerays-khr");
#else
    return Status::Fail(ErrorCode::Unavailable,
                                            "TryVkTraceRaysDemoStub Unavailable SKIP: ENGINE_WITH_VULKAN=0");
#endif
}

}  // namespace engine::rt
