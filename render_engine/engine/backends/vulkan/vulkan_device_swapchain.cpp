#include "vulkan_device_internal.h"

namespace engine::rhi {

Status VulkanDevice::CreateInstance() {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "render_engine";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "render_engine";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_1;

#if defined(_WIN32)
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
#elif defined(__linux__)
    // W16: enable both xlib + wayland; unused surface ext is fine if present.
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
                                                VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
#else
    const char* exts[] = {VK_KHR_SURFACE_EXTENSION_NAME};
#endif
    const char* layers[] = {"VK_LAYER_KHRONOS_validation"};

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
#if defined(__linux__)
    ci.enabledExtensionCount = 3;
#else
    ci.enabledExtensionCount = 2;
#endif
    ci.ppEnabledExtensionNames = exts;
    if (enable_validation_) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = layers;
    }

    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS && enable_validation_) {
        LogWarn("Vulkan validation layers unavailable — retry without");
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;
        r = vkCreateInstance(&ci, nullptr, &instance_);
    }
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateInstance failed: " + VkErr(r));
    }
    return Status::Ok();
}

Status VulkanDevice::CreateSurface() {
#if defined(_WIN32)
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandleW(nullptr);
    ci.hwnd = hwnd_;
    const VkResult r = vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface_);
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateWin32SurfaceKHR failed: " + VkErr(r));
    }
    return Status::Ok();
#elif defined(__linux__)
    if (wayland_) {
#if defined(ENGINE_HAS_WAYLAND)
        VkWaylandSurfaceCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        ci.display = static_cast<wl_display*>(wayland_->display);
        ci.surface = static_cast<wl_surface*>(wayland_->surface);
        const VkResult r = vkCreateWaylandSurfaceKHR(instance_, &ci, nullptr, &surface_);
        if (r != VK_SUCCESS) {
            return Status::Fail("vkCreateWaylandSurfaceKHR failed: " + VkErr(r));
        }
        return Status::Ok("wayland-surface");
#else
        return Status::Fail(ErrorCode::Unavailable, "CreateSurface: ENGINE_HAS_WAYLAND off");
#endif
    }
    return TryCreateXlibSurface(instance_, x11_->display, x11_->window, &surface_);
#else
    return Status::Fail(ErrorCode::Unavailable, "CreateSurface: unsupported platform");
#endif
}

Status VulkanDevice::PickPhysicalDevice() {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        return Status::Fail("No Vulkan physical devices");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    struct Candidate {
        VkPhysicalDevice pd = VK_NULL_HANDLE;
        std::uint32_t graphics = 0;
        std::uint32_t present = 0;
        int index = 0;
        bool discrete = false;
        std::uint64_t vram = 0;
        std::string name;
    };
    std::vector<Candidate> candidates;

    for (std::uint32_t di = 0; di < count; ++di) {
        VkPhysicalDevice pd = devices[di];
        std::uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(pd, &qcount, qprops.data());

        std::int32_t graphics = -1;
        std::int32_t present = -1;
        for (std::uint32_t i = 0; i < qcount; ++i) {
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && graphics < 0) {
                graphics = static_cast<std::int32_t>(i);
            }
            VkBool32 support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &support);
            if (support && present < 0) {
                present = static_cast<std::int32_t>(i);
            }
            if (graphics >= 0 && present >= 0 && graphics == present) {
                break;
            }
        }
        if (graphics < 0 || present < 0) {
            continue;
        }

        std::uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> exts(ext_count);
        vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
        bool has_swapchain = false;
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                has_swapchain = true;
                break;
            }
        }
        if (!has_swapchain) {
            continue;
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(pd, &mem);
        std::uint64_t vram = 0;
        for (std::uint32_t mi = 0; mi < mem.memoryHeapCount; ++mi) {
            if (mem.memoryHeaps[mi].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
                vram += mem.memoryHeaps[mi].size;
            }
        }

        Candidate c;
        c.pd = pd;
        c.graphics = static_cast<std::uint32_t>(graphics);
        c.present = static_cast<std::uint32_t>(present);
        c.index = static_cast<int>(di);
        c.discrete = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        c.vram = vram;
        c.name = props.deviceName;
        candidates.push_back(std::move(c));
    }

    if (candidates.empty()) {
        return Status::Fail("No suitable Vulkan GPU with swapchain + present");
    }

    const Candidate* chosen = nullptr;
    if (adapter_index_ >= 0) {
        for (const auto& c : candidates) {
            if (c.index == adapter_index_) {
                chosen = &c;
                break;
            }
        }
        if (!chosen) {
            return Status::Fail("Vulkan adapter_index=" + std::to_string(adapter_index_) +
                                                    " not suitable (use --list-gpus)");
        }
    } else {
        // Match D3D HIGH_PERFORMANCE preference: discrete first, then most VRAM.
        chosen = &candidates[0];
        for (const auto& c : candidates) {
            if (c.discrete && !chosen->discrete) {
                chosen = &c;
            } else if (c.discrete == chosen->discrete && c.vram > chosen->vram) {
                chosen = &c;
            }
        }
    }

    physical_ = chosen->pd;
    graphics_family_ = chosen->graphics;
    present_family_ = chosen->present;
    vkGetPhysicalDeviceMemoryProperties(physical_, &mem_props_);
    LogInfo(std::string("Vulkan adapter[") + std::to_string(chosen->index) +
                    (chosen->discrete ? " discrete]: " : "]: ") + chosen->name + " vram≈" +
                    std::to_string(chosen->vram / (1024ull * 1024ull)) + "MB");
    return Status::Ok();
}

Status VulkanDevice::CreateLogicalDevice() {
    const float priority = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    const std::uint32_t families[] = {graphics_family_, present_family_};
    for (std::uint32_t fam : families) {
        bool exists = false;
        for (const auto& q : qcis) {
            if (q.queueFamilyIndex == fam) {
                exists = true;
                break;
            }
        }
        if (exists) {
            continue;
        }
        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = fam;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);
    }

    // Probe descriptor-indexing (capability doc only; Feature bindless stays unset / SKIP).
    descriptor_indexing_available_ = false;
    {
        uint32_t ext_count = 0;
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> dexts(ext_count);
        vkEnumerateDeviceExtensionProperties(physical_, nullptr, &ext_count, dexts.data());
        for (const auto& e : dexts) {
            if (std::strcmp(e.extensionName, "VK_EXT_descriptor_indexing") == 0 ||
                    std::strcmp(e.extensionName, VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME) == 0) {
                descriptor_indexing_available_ = true;
                break;
            }
        }
    }

    const char* exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceFeatures available{};
    vkGetPhysicalDeviceFeatures(physical_, &available);
    if (available.depthClamp) {
        features.depthClamp = VK_TRUE;
        depth_clamp_enabled_ = true;
    } else {
        depth_clamp_enabled_ = false;
        LogWarn("Vulkan depthClamp unavailable; transparent lit uses clip (may differ from D3D)");
    }

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<std::uint32_t>(qcis.size());
    ci.pQueueCreateInfos = qcis.data();
    ci.pEnabledFeatures = &features;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = exts;

    const VkResult r = vkCreateDevice(physical_, &ci, nullptr, &device_);
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateDevice failed: " + VkErr(r));
    }
    vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
    vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphics_family_;
    if (vkCreateCommandPool(device_, &pool, nullptr, &command_pool_) != VK_SUCCESS) {
        return Status::Fail("vkCreateCommandPool failed");
    }

    command_buffers_.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = command_pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    if (vkAllocateCommandBuffers(device_, &alloc, command_buffers_.data()) != VK_SUCCESS) {
        return Status::Fail("vkAllocateCommandBuffers failed");
    }
    return Status::Ok();
}

Status VulkanDevice::CreateSwapchain() {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);

    std::uint32_t format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &format_count, formats.data());

    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    surface_format_ = chosen;

    std::uint32_t present_count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_count, nullptr);
    std::vector<VkPresentModeKHR> modes(present_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &present_count, modes.data());
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
    if (vsync_) {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
    } else {
        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (auto m : modes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = m;
                break;
            }
        }
        if (present_mode == VK_PRESENT_MODE_FIFO_KHR) {
            for (auto m : modes) {
                if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                    present_mode = m;
                    break;
                }
            }
        }
    }
    LogInfo(std::string("Vulkan presentMode=") +
                    (present_mode == VK_PRESENT_MODE_MAILBOX_KHR
                             ? "MAILBOX"
                             : present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" : "FIFO") +
                    (vsync_ ? " (vsync)" : " (uncapped)"));

    VkExtent2D extent{};
    if (caps.currentExtent.width != UINT32_MAX) {
        extent = caps.currentExtent;
    } else {
        extent.width = std::clamp(width_, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(height_, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    width_ = extent.width;
    height_ = extent.height;

    std::uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
        image_count = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = image_count;
    ci.imageFormat = surface_format_.format;
    ci.imageColorSpace = surface_format_.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const std::uint32_t qf[] = {graphics_family_, present_family_};
    if (graphics_family_ != present_family_) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices = qf;
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = present_mode;
    ci.clipped = VK_TRUE;

    const VkResult r = vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_);
    if (r != VK_SUCCESS) {
        return Status::Fail("vkCreateSwapchainKHR failed: " + VkErr(r));
    }

    std::uint32_t img_count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, nullptr);
    swapchain_images_.resize(img_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, swapchain_images_.data());

    swapchain_views_.resize(img_count);
    for (std::uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = swapchain_images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = surface_format_.format;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &vi, nullptr, &swapchain_views_[i]) != VK_SUCCESS) {
            return Status::Fail("vkCreateImageView failed");
        }
    }
    return Status::Ok();
}

void VulkanDevice::DestroySwapchainViews() {
    for (VkImageView v : swapchain_views_) {
        if (v != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, v, nullptr);
        }
    }
    swapchain_views_.clear();
    swapchain_images_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanDevice::DestroySwapchain() {
    DestroyFramebuffersOnly();
    DestroyPostFramebuffersOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
}

Status VulkanDevice::RecreateSwapchain() {
    if (device_ == VK_NULL_HANDLE) {
        return Status::Fail("No device");
    }
    vkDeviceWaitIdle(device_);
    DestroyFramebuffersOnly();
    DestroyPostFramebuffersOnly();
    DestroySceneColorOnly();
    DestroyHistoryOnly();
    DestroyDepthOnly();
    DestroySwapchainViews();
    if (lit_ready_) {
        DestroyPresentRenderPasses();
    }
    if (auto st = CreateSwapchain(); !st) {
        return st;
    }
    if (lit_ready_) {
        if (auto st = CreateDepthResources(); !st) {
            return st;
        }
        if (auto st = CreatePresentRenderPass(); !st) {
            return st;
        }
        if (auto st = EnsureSceneColor(); !st) {
            return st;
        }
        if (post_stub_ready_) {
            if (auto st = EnsureHistory(); !st) {
                return st;
            }
        }
        if (auto st = CreateFramebuffers(); !st) {
            return st;
        }
        if (post_stub_ready_) {
            if (auto st = CreatePostFramebuffers(); !st) {
                return st;
            }
        }
    }
    return Status::Ok();
}

}  // namespace engine::rhi
