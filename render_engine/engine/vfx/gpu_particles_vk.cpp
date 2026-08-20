#include "engine/vfx/gpu_particles_internal.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::vfx {
namespace particles_detail {

struct ParticleGpu {
    float px, py, pz, life;
    float vx, vy, vz, size;
    float cr, cg, cb, ca;
};

#if defined(ENGINE_WITH_VULKAN) && ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>

std::filesystem::path ResolveParticleCsSpirv() {
#if defined(ENGINE_SHADER_DIR_A)
    const auto from_def =
            std::filesystem::path(ENGINE_SHADER_DIR_A) / "particle_integrate_cs_vk.cs.spv";
    if (std::filesystem::exists(from_def)) {
        return from_def;
    }
#endif
    if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
        const auto from_env = std::filesystem::path(env) / "particle_integrate_cs_vk.cs.spv";
        if (std::filesystem::exists(from_env)) {
            return from_env;
        }
    }
    return {};
}

Status TryIntegrateGpuCsVk(std::vector<Particle>& particles, float dt) {
    if (particles.empty()) {
        return Status::Ok("gpu-cs-vk-empty");
    }
    const auto cs_path = ResolveParticleCsSpirv();
    if (cs_path.empty()) {
        return Status::Fail(ErrorCode::Unavailable, "particle_integrate_cs_vk.cs.spv not found");
    }
    std::vector<char> cs_bytes;
    {
        std::ifstream in(cs_path, std::ios::binary);
        if (!in) {
            return Status::Fail(ErrorCode::Unavailable, "failed to read particle SPIR-V");
        }
        cs_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        if (cs_bytes.empty() || (cs_bytes.size() % 4) != 0) {
            return Status::Fail(ErrorCode::Unavailable, "invalid particle SPIR-V");
        }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "engine_gpu_particle_vk";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS) {
        return Status::Fail(ErrorCode::Unavailable, "vkCreateInstance failed");
    }
    uint32_t phys_count = 0;
    vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
    if (phys_count == 0) {
        vkDestroyInstance(instance, nullptr);
        return Status::Fail(ErrorCode::Unavailable, "no Vulkan GPU");
    }
    std::vector<VkPhysicalDevice> phys(phys_count);
    vkEnumeratePhysicalDevices(instance, &phys_count, phys.data());
    VkPhysicalDevice physical = phys[0];
    uint32_t qf_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qf_count, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qf_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &qf_count, qfs.data());
    uint32_t compute_family = UINT32_MAX;
    for (uint32_t i = 0; i < qf_count; ++i) {
        if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            compute_family = i;
            break;
        }
    }
    if (compute_family == UINT32_MAX) {
        vkDestroyInstance(instance, nullptr);
        return Status::Fail(ErrorCode::Unavailable, "no compute queue");
    }
    float prio = 1.f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = compute_family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    VkDevice device = VK_NULL_HANDLE;
    if (vkCreateDevice(physical, &dci, nullptr, &device) != VK_SUCCESS) {
        vkDestroyInstance(instance, nullptr);
        return Status::Fail(ErrorCode::Unavailable, "vkCreateDevice failed");
    }
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, compute_family, 0, &queue);

    auto destroy_dev = [&]() {
        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(instance, nullptr);
    };

    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = cs_bytes.size();
    smci.pCode = reinterpret_cast<const uint32_t*>(cs_bytes.data());
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) {
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vkCreateShaderModule failed");
    }

    // W17 ADR 0041: SSBO + push-constant dispatch (parity with D3D12 UAV path).
    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo dsl{};
    dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dsl.bindingCount = 1;
    dsl.pBindings = &bind;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(device, &dsl, nullptr, &set_layout) != VK_SUCCESS) {
        vkDestroyShaderModule(device, module, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle set layout failed");
    }

    VkPushConstantRange pc_range{};
    pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc_range.offset = 0;
    pc_range.size = 16;
    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &set_layout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pc_range;
    VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &pipe_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        vkDestroyShaderModule(device, module, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle pipeline layout failed");
    }

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = module;
    cpci.stage.pName = "CSMain";
    cpci.layout = pipe_layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline) !=
            VK_SUCCESS) {
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        vkDestroyShaderModule(device, module, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle CS PSO failed");
    }
    vkDestroyShaderModule(device, module, nullptr);
    module = VK_NULL_HANDLE;

    VkDescriptorPoolSize pool_sz{};
    pool_sz.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_sz.descriptorCount = 1;
    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_sz;
    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &dpci, nullptr, &desc_pool) != VK_SUCCESS) {
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle desc pool failed");
    }
    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = desc_pool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &set_layout;
    VkDescriptorSet desc_set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device, &dsai, &desc_set) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle desc set failed");
    }

    const uint32_t n = static_cast<uint32_t>(particles.size());
    std::vector<ParticleGpu> upload(n);
    for (uint32_t i = 0; i < n; ++i) {
        upload[i].px = particles[i].position.x;
        upload[i].py = particles[i].position.y;
        upload[i].pz = particles[i].position.z;
        upload[i].life = particles[i].life;
        upload[i].vx = particles[i].velocity.x;
        upload[i].vy = particles[i].velocity.y;
        upload[i].vz = particles[i].velocity.z;
        upload[i].size = particles[i].size;
        upload[i].cr = particles[i].color.r;
        upload[i].cg = particles[i].color.g;
        upload[i].cb = particles[i].color.b;
        upload[i].ca = particles[i].color.a;
    }
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(n) * sizeof(ParticleGpu);

    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &mem_props);
    auto find_mem = [&](uint32_t type_bits, VkMemoryPropertyFlags flags) -> uint32_t {
        for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
            if ((type_bits & (1u << i)) &&
                    (mem_props.memoryTypes[i].propertyFlags & flags) == flags) {
                return i;
            }
        }
        return UINT32_MAX;
    };

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer ssbo = VK_NULL_HANDLE;
    if (vkCreateBuffer(device, &bci, nullptr, &ssbo) != VK_SUCCESS) {
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle buffer failed");
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, ssbo, &req);
    const uint32_t mem_type =
            find_mem(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(device, ssbo, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Unavailable, "vk particle no host-visible memory");
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;
    VkDeviceMemory ssbo_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(device, &mai, nullptr, &ssbo_mem) != VK_SUCCESS ||
            vkBindBufferMemory(device, ssbo, ssbo_mem, 0) != VK_SUCCESS) {
        if (ssbo_mem) {
            vkFreeMemory(device, ssbo_mem, nullptr);
        }
        vkDestroyBuffer(device, ssbo, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle bind memory failed");
    }

    {
        void* mapped = nullptr;
        if (vkMapMemory(device, ssbo_mem, 0, bytes, 0, &mapped) != VK_SUCCESS || !mapped) {
            vkDestroyBuffer(device, ssbo, nullptr);
            vkFreeMemory(device, ssbo_mem, nullptr);
            vkDestroyDescriptorPool(device, desc_pool, nullptr);
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, pipe_layout, nullptr);
            vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
            destroy_dev();
            return Status::Fail(ErrorCode::Failed, "vk particle map upload failed");
        }
        std::memcpy(mapped, upload.data(), static_cast<size_t>(bytes));
        vkUnmapMemory(device, ssbo_mem);
    }

    VkDescriptorBufferInfo dbi{};
    dbi.buffer = ssbo;
    dbi.offset = 0;
    dbi.range = bytes;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    VkCommandPoolCreateInfo cpci_pool{};
    cpci_pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci_pool.queueFamilyIndex = compute_family;
    cpci_pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(device, &cpci_pool, nullptr, &cmd_pool) != VK_SUCCESS) {
        vkDestroyBuffer(device, ssbo, nullptr);
        vkFreeMemory(device, ssbo_mem, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle cmd pool failed");
    }
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cbai, &cmd) != VK_SUCCESS) {
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        vkDestroyBuffer(device, ssbo, nullptr);
        vkFreeMemory(device, ssbo_mem, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle cmd alloc failed");
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout, 0, 1, &desc_set, 0,
                                                    nullptr);
    float consts[4] = {dt, static_cast<float>(n), 0.f, 0.f};
    vkCmdPushConstants(cmd, pipe_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(consts), consts);
    vkCmdDispatch(cmd, (n + 63u) / 64u, 1, 1);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                             &barrier, 0, nullptr, 0, nullptr);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device, &fci, nullptr, &fence) != VK_SUCCESS) {
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        vkDestroyBuffer(device, ssbo, nullptr);
        vkFreeMemory(device, ssbo_mem, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle fence failed");
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    if (vkQueueSubmit(queue, 1, &submit, fence) != VK_SUCCESS ||
            vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        vkDestroyFence(device, fence, nullptr);
        vkDestroyCommandPool(device, cmd_pool, nullptr);
        vkDestroyBuffer(device, ssbo, nullptr);
        vkFreeMemory(device, ssbo_mem, nullptr);
        vkDestroyDescriptorPool(device, desc_pool, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipelineLayout(device, pipe_layout, nullptr);
        vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
        destroy_dev();
        return Status::Fail(ErrorCode::Failed, "vk particle submit/wait failed");
    }

    {
        void* mapped = nullptr;
        if (vkMapMemory(device, ssbo_mem, 0, bytes, 0, &mapped) == VK_SUCCESS && mapped) {
            const auto* src = static_cast<const ParticleGpu*>(mapped);
            for (uint32_t i = 0; i < n; ++i) {
                particles[i].position = {src[i].px, src[i].py, src[i].pz};
                particles[i].life = src[i].life;
                particles[i].velocity = {src[i].vx, src[i].vy, src[i].vz};
                particles[i].size = src[i].size;
                particles[i].color = {src[i].cr, src[i].cg, src[i].cb, src[i].ca};
            }
            vkUnmapMemory(device, ssbo_mem);
        }
    }

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmd_pool, nullptr);
    vkDestroyBuffer(device, ssbo, nullptr);
    vkFreeMemory(device, ssbo_mem, nullptr);
    vkDestroyDescriptorPool(device, desc_pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    destroy_dev();
    (void)queue;
    return Status::Ok("gpu-cs-vk");
}
#else
Status TryIntegrateGpuCsVk(std::vector<Particle>&, float) {
    return Status::Fail(ErrorCode::Unavailable, "Vulkan particle CS not available");
}
#endif

}  // namespace particles_detail
}  // namespace engine::vfx
