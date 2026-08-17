#include "engine/animation/gpu_skin_vk.h"

#include "engine/core/feature.h"
#include "engine/core/log.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#if ENGINE_WITH_VULKAN
#include <vulkan/vulkan.h>
#endif

namespace engine::animation {
namespace {

std::filesystem::path ResolveSkinCsSpirv(const std::filesystem::path& cs_spirv) {
  if (!cs_spirv.empty()) {
    return cs_spirv;
  }
#if defined(ENGINE_SHADER_DIR_A)
  const auto from_def = std::filesystem::path(ENGINE_SHADER_DIR_A) / "skin_cs_vk.cs.spv";
  if (std::filesystem::exists(from_def)) {
    return from_def;
  }
#endif
  if (const char* env = std::getenv("ENGINE_SHADER_DIR")) {
    const auto from_env = std::filesystem::path(env) / "skin_cs_vk.cs.spv";
    if (std::filesystem::exists(from_env)) {
      return from_env;
    }
  }
  return {};
}

#if ENGINE_WITH_VULKAN

bool LoadFileBytes(const std::filesystem::path& path, std::vector<char>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !out.empty() && (out.size() % 4) == 0;
}

Status DispatchGpuSkinVkImpl(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                             const std::vector<int>& bones4, const std::vector<float>& weights4,
                             std::vector<Vec3>& out_positions,
                             const std::filesystem::path& cs_spirv) {
  if (!QueryFeature("gpu_skinning")) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: Feature gpu_skinning off");
  }
  if (bind_positions.empty()) {
    out_positions.clear();
    return Status::Ok();
  }
  const auto cs_path = ResolveSkinCsSpirv(cs_spirv);
  if (cs_path.empty()) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: skin_cs_vk.cs.spv not found");
  }
  std::vector<char> cs_bytes;
  if (!LoadFileBytes(cs_path, cs_bytes)) {
    return Status::Fail(ErrorCode::Unavailable,
                        "DispatchGpuSkinVk: failed to read " + cs_path.string());
  }

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice phys = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queue_family = 0;

  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "engine_gpu_skin_vk";
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ici.pApplicationInfo = &app;
  if (vkCreateInstance(&ici, nullptr, &instance) != VK_SUCCESS || !instance) {
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: vkCreateInstance failed");
  }

  uint32_t phys_count = 0;
  vkEnumeratePhysicalDevices(instance, &phys_count, nullptr);
  std::vector<VkPhysicalDevice> phys_list(phys_count);
  vkEnumeratePhysicalDevices(instance, &phys_count, phys_list.data());
  for (VkPhysicalDevice cand : phys_list) {
    uint32_t qcount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(cand, &qcount, nullptr);
    std::vector<VkQueueFamilyProperties> qprops(qcount);
    vkGetPhysicalDeviceQueueFamilyProperties(cand, &qcount, qprops.data());
    for (uint32_t i = 0; i < qcount; ++i) {
      if (qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        phys = cand;
        queue_family = i;
        break;
      }
    }
    if (phys) {
      break;
    }
  }
  if (!phys) {
    vkDestroyInstance(instance, nullptr);
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: no compute-capable GPU");
  }

  float prio = 1.f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queue_family;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  if (vkCreateDevice(phys, &dci, nullptr, &device) != VK_SUCCESS || !device) {
    vkDestroyInstance(instance, nullptr);
    return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: vkCreateDevice failed");
  }
  vkGetDeviceQueue(device, queue_family, 0, &queue);

  auto fail_cleanup = [&](const char* msg) -> Status {
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return Status::Fail(ErrorCode::Failed, msg);
  };

  VkPhysicalDeviceMemoryProperties mem_props{};
  vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
  auto find_host_mem = [&](uint32_t type_bits) -> uint32_t {
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
      if ((type_bits & (1u << i)) == 0) {
        continue;
      }
      const auto flags = mem_props.memoryTypes[i].propertyFlags;
      if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        return i;
      }
    }
    return UINT32_MAX;
  };

  struct Buf {
    VkBuffer buf = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
  };
  auto create_buf = [&](VkDeviceSize size, VkBufferUsageFlags usage, Buf& out) -> bool {
    out.size = size;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bci, nullptr, &out.buf) != VK_SUCCESS) {
      return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, out.buf, &req);
    const uint32_t mi = find_host_mem(req.memoryTypeBits);
    if (mi == UINT32_MAX) {
      return false;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mi;
    if (vkAllocateMemory(device, &mai, nullptr, &out.mem) != VK_SUCCESS) {
      return false;
    }
    if (vkBindBufferMemory(device, out.buf, out.mem, 0) != VK_SUCCESS) {
      return false;
    }
    return true;
  };
  auto destroy_buf = [&](Buf& b) {
    if (b.buf) {
      vkDestroyBuffer(device, b.buf, nullptr);
    }
    if (b.mem) {
      vkFreeMemory(device, b.mem, nullptr);
    }
    b = {};
  };
  auto map_upload = [&](Buf& b, const void* data, size_t bytes) {
    void* mapped = nullptr;
    if (vkMapMemory(device, b.mem, 0, b.size, 0, &mapped) == VK_SUCCESS && mapped) {
      std::memcpy(mapped, data, bytes);
      vkUnmapMemory(device, b.mem);
    }
  };

  const uint32_t n = static_cast<uint32_t>(bind_positions.size());
  const uint32_t bone_count =
      pose.bone_matrices.empty() ? 1u : static_cast<uint32_t>(pose.bone_matrices.size());

  struct Align16 {
    float m[16];
  };
  std::vector<Align16> bone_upload(bone_count);
  for (uint32_t i = 0; i < bone_count; ++i) {
    if (i < pose.bone_matrices.size()) {
      std::memcpy(bone_upload[i].m, pose.bone_matrices[i].m.data(), sizeof(float) * 16);
    } else {
      const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      std::memcpy(bone_upload[i].m, id, sizeof(id));
    }
  }
  struct Int4 {
    int v[4];
  };
  struct Float4 {
    float v[4];
  };
  std::vector<Int4> idx_upload(n);
  std::vector<Float4> w_upload(n);
  std::vector<float> pos_upload(static_cast<size_t>(n) * 3ull);
  for (uint32_t i = 0; i < n; ++i) {
    pos_upload[i * 3 + 0] = bind_positions[i].x;
    pos_upload[i * 3 + 1] = bind_positions[i].y;
    pos_upload[i * 3 + 2] = bind_positions[i].z;
    idx_upload[i] = {0, 0, 0, 0};
    w_upload[i] = {1.f, 0.f, 0.f, 0.f};
    if (bones4.size() >= (i + 1ull) * 4 && weights4.size() >= (i + 1ull) * 4) {
      for (int k = 0; k < 4; ++k) {
        idx_upload[i].v[k] = bones4[i * 4ull + static_cast<std::size_t>(k)];
        w_upload[i].v[k] = weights4[i * 4ull + static_cast<std::size_t>(k)];
      }
    }
  }

  struct SkinCBHost {
    uint32_t vertex_count;
    uint32_t pad[3];
  } skin_cb{n, {0, 0, 0}};

  const VkDeviceSize pos_bytes = static_cast<VkDeviceSize>(n) * 12ull;
  const VkDeviceSize bone_bytes = static_cast<VkDeviceSize>(bone_count) * 64ull;
  const VkDeviceSize idx_bytes = static_cast<VkDeviceSize>(n) * 16ull;
  const VkDeviceSize w_bytes = static_cast<VkDeviceSize>(n) * 16ull;
  const VkDeviceSize cb_bytes = 16;

  Buf cb_buf{}, bind_buf{}, bone_buf{}, idx_buf{}, w_buf{}, out_buf{};
  const VkBufferUsageFlags storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  const VkBufferUsageFlags uniform = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (!create_buf(cb_bytes, uniform, cb_buf) || !create_buf(pos_bytes, storage, bind_buf) ||
      !create_buf(bone_bytes, storage, bone_buf) || !create_buf(idx_bytes, storage, idx_buf) ||
      !create_buf(w_bytes, storage, w_buf) || !create_buf(pos_bytes, storage, out_buf)) {
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: buffer create failed");
  }
  map_upload(cb_buf, &skin_cb, sizeof(skin_cb));
  map_upload(bind_buf, pos_upload.data(), static_cast<size_t>(pos_bytes));
  map_upload(bone_buf, bone_upload.data(), static_cast<size_t>(bone_bytes));
  map_upload(idx_buf, idx_upload.data(), static_cast<size_t>(idx_bytes));
  map_upload(w_buf, w_upload.data(), static_cast<size_t>(w_bytes));

  VkShaderModuleCreateInfo smci{};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smci.codeSize = cs_bytes.size();
  smci.pCode = reinterpret_cast<const uint32_t*>(cs_bytes.data());
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &smci, nullptr, &module) != VK_SUCCESS) {
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: shader module failed");
  }

  VkDescriptorSetLayoutBinding binds[6]{};
  binds[0].binding = 0;
  binds[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  binds[0].descriptorCount = 1;
  binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  for (uint32_t i = 1; i <= 5; ++i) {
    binds[i].binding = i;
    binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[i].descriptorCount = 1;
    binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dslci{};
  dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  dslci.bindingCount = 6;
  dslci.pBindings = binds;
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  if (vkCreateDescriptorSetLayout(device, &dslci, nullptr, &set_layout) != VK_SUCCESS) {
    vkDestroyShaderModule(device, module, nullptr);
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: set layout failed");
  }

  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &set_layout;
  VkPipelineLayout pipe_layout = VK_NULL_HANDLE;
  if (vkCreatePipelineLayout(device, &plci, nullptr, &pipe_layout) != VK_SUCCESS) {
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: pipeline layout failed");
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
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: create CS pipeline failed");
  }

  VkDescriptorPoolSize sizes[2]{};
  sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  sizes[0].descriptorCount = 1;
  sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  sizes[1].descriptorCount = 5;
  VkDescriptorPoolCreateInfo dpci{};
  dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.maxSets = 1;
  dpci.poolSizeCount = 2;
  dpci.pPoolSizes = sizes;
  VkDescriptorPool pool = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &dpci, nullptr, &pool) != VK_SUCCESS) {
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: descriptor pool failed");
  }
  VkDescriptorSetAllocateInfo dsai{};
  dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dsai.descriptorPool = pool;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = &set_layout;
  if (vkAllocateDescriptorSets(device, &dsai, &set) != VK_SUCCESS) {
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: allocate set failed");
  }

  VkDescriptorBufferInfo bis[6]{};
  Buf* bufs[6] = {&cb_buf, &bind_buf, &bone_buf, &idx_buf, &w_buf, &out_buf};
  for (int i = 0; i < 6; ++i) {
    bis[i].buffer = bufs[i]->buf;
    bis[i].offset = 0;
    bis[i].range = bufs[i]->size;
  }
  VkWriteDescriptorSet writes[6]{};
  for (int i = 0; i < 6; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = set;
    writes[i].dstBinding = static_cast<uint32_t>(i);
    writes[i].descriptorCount = 1;
    writes[i].descriptorType =
        i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = &bis[i];
  }
  vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);

  VkCommandPoolCreateInfo cpoci{};
  cpoci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpoci.queueFamilyIndex = queue_family;
  VkCommandPool cmd_pool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(device, &cpoci, nullptr, &cmd_pool) != VK_SUCCESS) {
    vkDestroyDescriptorPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipe_layout, nullptr);
    vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
    vkDestroyShaderModule(device, module, nullptr);
    destroy_buf(cb_buf);
    destroy_buf(bind_buf);
    destroy_buf(bone_buf);
    destroy_buf(idx_buf);
    destroy_buf(w_buf);
    destroy_buf(out_buf);
    return fail_cleanup("DispatchGpuSkinVk: command pool failed");
  }
  VkCommandBufferAllocateInfo cbai{};
  cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.commandPool = cmd_pool;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &cbai, &cmd);

  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout, 0, 1, &set, 0, nullptr);
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
  vkCreateFence(device, &fci, nullptr, &fence);
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  vkQueueSubmit(queue, 1, &si, fence);
  vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

  out_positions.resize(n);
  {
    void* mapped = nullptr;
    if (vkMapMemory(device, out_buf.mem, 0, out_buf.size, 0, &mapped) == VK_SUCCESS && mapped) {
      const auto* src = static_cast<const float*>(mapped);
      for (uint32_t i = 0; i < n; ++i) {
        out_positions[i] = Vec3{src[i * 3 + 0], src[i * 3 + 1], src[i * 3 + 2]};
      }
      vkUnmapMemory(device, out_buf.mem);
    }
  }

  vkDestroyFence(device, fence, nullptr);
  vkDestroyCommandPool(device, cmd_pool, nullptr);
  vkDestroyDescriptorPool(device, pool, nullptr);
  vkDestroyPipeline(device, pipeline, nullptr);
  vkDestroyPipelineLayout(device, pipe_layout, nullptr);
  vkDestroyDescriptorSetLayout(device, set_layout, nullptr);
  vkDestroyShaderModule(device, module, nullptr);
  destroy_buf(cb_buf);
  destroy_buf(bind_buf);
  destroy_buf(bone_buf);
  destroy_buf(idx_buf);
  destroy_buf(w_buf);
  destroy_buf(out_buf);
  vkDestroyDevice(device, nullptr);
  vkDestroyInstance(instance, nullptr);

  LogInfo("TryDispatchGpuSkinVk: CS skinned " + std::to_string(n) + " verts via " +
          cs_path.string());
  return Status::Ok();
}

#endif  // ENGINE_WITH_VULKAN

}  // namespace

Status DispatchGpuSkinVkStatus(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                               const std::vector<int>& bones4, const std::vector<float>& weights4,
                               std::vector<Vec3>& out_positions,
                               const std::filesystem::path& cs_spirv) {
#if ENGINE_WITH_VULKAN
  return DispatchGpuSkinVkImpl(bind_positions, pose, bones4, weights4, out_positions, cs_spirv);
#else
  (void)bind_positions;
  (void)pose;
  (void)bones4;
  (void)weights4;
  (void)out_positions;
  (void)cs_spirv;
  return Status::Fail(ErrorCode::Unavailable, "DispatchGpuSkinVk: Vulkan disabled (SKIP)");
#endif
}

bool TryDispatchGpuSkinVk(const std::vector<Vec3>& bind_positions, const SkinPose& pose,
                          const std::vector<int>& bones4, const std::vector<float>& weights4,
                          std::vector<Vec3>& out_positions,
                          const std::filesystem::path& cs_spirv) {
  return DispatchGpuSkinVkStatus(bind_positions, pose, bones4, weights4, out_positions, cs_spirv)
      .ok();
}

}  // namespace engine::animation
