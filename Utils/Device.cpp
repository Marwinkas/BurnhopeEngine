#define VMA_IMPLEMENTATION
#include "Device.hpp"
#include "Core/Types.hpp"
#include "GpuSync.hpp"
#include "VkExtDispatch.hpp"
#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <unordered_set>

namespace burnhope {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
  const char* level = "INFO";
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    level = "ERROR";
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    level = "WARNING";
  }
  const char* kind = "General";
  if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    kind = "Validation";
  } else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    kind = "Performance";
  }
  if (data && data->pMessage) {
    std::fprintf(stderr, "[Vulkan %s/%s] %s\n", level, kind, data->pMessage);
    std::fflush(stderr);
  }
  return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* createInfo,
    const VkAllocationCallbacks* allocator,
    VkDebugUtilsMessengerEXT* messenger) {
  const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  return func ? func(instance, createInfo, allocator, messenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT messenger,
    const VkAllocationCallbacks* allocator) {
  const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (func) {
    func(instance, messenger, allocator);
  }
}

} // namespace

BurnhopeDevice::BurnhopeDevice(BurnhopeWindow& window) : window_{window} {
  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createCommandPool();
  createGlobalSamplers();
  createTimelineSemaphore();
  queryCaps();
  properties = caps_.properties;

  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (vkCreateFence(device_, &fenceInfo, nullptr, &uploadFence_) != VK_SUCCESS) {
    throwVkError("BurnhopeDevice: upload fence");
  }
}

BurnhopeDevice::~BurnhopeDevice() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  if (uploadFence_) {
    vkDestroyFence(device_, uploadFence_, nullptr);
  }
  if (frameTimeline_) {
    vkDestroySemaphore(device_, frameTimeline_, nullptr);
  }
  destroyGlobalSamplers();
  if (commandPool_) {
    vkDestroyCommandPool(device_, commandPool_, nullptr);
  }
  if (allocator_) {
    vmaDestroyAllocator(allocator_);
  }
  if (device_) {
    vkDestroyDevice(device_, nullptr);
  }
  if (kEnableValidationLayers && debugMessenger_) {
    DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
  }
  if (surface_) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  }
  if (instance_) {
    vkDestroyInstance(instance_, nullptr);
  }
}

void BurnhopeDevice::createInstance() {
  if (kEnableValidationLayers && !checkValidationLayerSupport()) {
    throwVkError("validation layers unavailable");
  }

  VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  appInfo.pApplicationName = "Burnhope Engine";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "Burnhope";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_4;

  auto extensions = requiredInstanceExtensions();
  VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  if (kEnableValidationLayers) {
    createInfo.enabledLayerCount = static_cast<uint32_t>(kValidationLayers.size());
    createInfo.ppEnabledLayerNames = kValidationLayers.data();
    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = &debugCreateInfo;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
    throwVkError("vkCreateInstance");
  }
  verifyRequiredInstanceExtensions();
  vkext::loadInstance(instance_);
  if (kEnableValidationLayers) {
    std::fprintf(stderr, "[Vulkan] Validation layers: ON (VK_LAYER_KHRONOS_validation)\n");
    std::fflush(stderr);
  }
}

void BurnhopeDevice::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) {
    throwVkError("no Vulkan GPU");
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());

  for (VkPhysicalDevice dev : devices) {
    if (isDeviceSuitable(dev)) {
      physicalDevice_ = dev;
      break;
    }
  }
  if (!physicalDevice_) {
    throwVkError("no suitable GPU");
  }
}

void BurnhopeDevice::createLogicalDevice() {
  const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);
  std::set<uint32_t> uniqueFamilies = {
      indices.graphicsFamily, indices.presentFamily, indices.computeFamily, indices.transferFamily};

  std::vector<VkDeviceQueueCreateInfo> queueInfos;
  float priority = 1.0f;
  for (uint32_t family : uniqueFamilies) {
    if (family == UINT32_MAX) {
      continue;
    }
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    queueInfos.push_back(q);
  }

  VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  VkPhysicalDeviceFeatures& f = features2.features;
  f.samplerAnisotropy = VK_TRUE;
  f.independentBlend = VK_TRUE;
  f.multiDrawIndirect = VK_TRUE;
  f.shaderInt64 = VK_TRUE;
  f.shaderInt16 = VK_TRUE;
  f.depthBounds = VK_TRUE;

  VkPhysicalDeviceVulkan14Features features14{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};
  features14.dynamicRenderingLocalRead = VK_TRUE;
  features14.hostImageCopy = VK_TRUE;
  features14.pushDescriptor = VK_TRUE;

  VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  features13.pNext = &features14;
  features13.dynamicRendering = VK_TRUE;
  features13.synchronization2 = VK_TRUE;
  features13.maintenance4 = VK_TRUE;
  features13.shaderDemoteToHelperInvocation = VK_TRUE;

  VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
  features12.pNext = &features13;
  features12.bufferDeviceAddress = VK_TRUE;
  features12.scalarBlockLayout = VK_TRUE;
  features12.storagePushConstant8 = VK_TRUE;
  features12.shaderInt8 = VK_TRUE;
  features12.descriptorIndexing = VK_TRUE;
  features12.runtimeDescriptorArray = VK_TRUE;
  features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  features12.descriptorBindingPartiallyBound = VK_TRUE;
  features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
  features12.timelineSemaphore = VK_TRUE;
  features12.shaderFloat16 = VK_TRUE;

  VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
  features11.shaderDrawParameters = VK_TRUE;
  features11.storageBuffer16BitAccess = VK_TRUE;
  features11.uniformAndStorageBuffer16BitAccess = VK_TRUE;
  features11.variablePointers = VK_TRUE;
  features11.variablePointersStorageBuffer = VK_TRUE;
  features11.pNext = &features12;

  VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
  rtpFeatures.rayTracingPipeline = VK_TRUE;

  VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
  asFeatures.pNext = &rtpFeatures;
  rtpFeatures.pNext = &features11;
  asFeatures.accelerationStructure = VK_TRUE;

  VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
  rqFeatures.pNext = &asFeatures;
  rqFeatures.rayQuery = VK_TRUE;

  VkPhysicalDeviceFragmentShadingRateFeaturesKHR vrs{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
  vrs.pNext = &rqFeatures;
  vrs.pipelineFragmentShadingRate = VK_TRUE;

  VkPhysicalDeviceMeshShaderFeaturesEXT mesh{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
  mesh.pNext = &vrs;
  mesh.meshShader = VK_TRUE;
  mesh.taskShader = VK_TRUE;

  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObj{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  shaderObj.pNext = &mesh;
  shaderObj.shaderObject = VK_TRUE;

  VkPhysicalDeviceDescriptorHeapFeaturesEXT heapFeat{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
  heapFeat.pNext = &shaderObj;
  heapFeat.descriptorHeap = VK_TRUE;

  VkPhysicalDevicePresentWaitFeaturesKHR presentWait{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
  presentWait.pNext = &heapFeat;
  presentWait.presentWait = VK_TRUE;

  VkPhysicalDeviceShaderUntypedPointersFeaturesKHR untypedPointers{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_UNTYPED_POINTERS_FEATURES_KHR};
  untypedPointers.pNext = &presentWait;
  untypedPointers.shaderUntypedPointers = VK_TRUE;

  VkPhysicalDeviceComputeShaderDerivativesFeaturesNV computeDeriv{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV};
  computeDeriv.pNext = &untypedPointers;
  computeDeriv.computeDerivativeGroupQuads = VK_TRUE;
  computeDeriv.computeDerivativeGroupLinear = VK_TRUE;

  features2.pNext = &computeDeriv;

  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.pNext = &features2;
  deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
  deviceInfo.pQueueCreateInfos = queueInfos.data();
  deviceInfo.enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size());
  deviceInfo.ppEnabledExtensionNames = kDeviceExtensions.data();

  if (vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_) != VK_SUCCESS) {
    throwVkError("vkCreateDevice");
  }
  vkext::loadDevice(device_);

  VmaAllocatorCreateInfo vmaInfo{};
  vmaInfo.physicalDevice = physicalDevice_;
  vmaInfo.device = device_;
  vmaInfo.instance = instance_;
  vmaInfo.vulkanApiVersion = VK_API_VERSION_1_4;
  vmaInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  vmaCreateAllocator(&vmaInfo, &allocator_);

  vkGetDeviceQueue(device_, indices.graphicsFamily, 0, &graphicsQueue_);
  vkGetDeviceQueue(device_, indices.presentFamily, 0, &presentQueue_);
  if (indices.computeFamily != UINT32_MAX) {
    vkGetDeviceQueue(device_, indices.computeFamily, 0, &computeQueue_);
  } else {
    computeQueue_ = graphicsQueue_;
  }
  if (indices.transferFamily != UINT32_MAX) {
    vkGetDeviceQueue(device_, indices.transferFamily, 0, &transferQueue_);
  } else {
    transferQueue_ = graphicsQueue_;
  }
}

void BurnhopeDevice::queryCaps() {
  vkGetPhysicalDeviceProperties(physicalDevice_, &caps_.properties);

  VkPhysicalDeviceShaderObjectFeaturesEXT shaderObj{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT};
  VkPhysicalDeviceDescriptorHeapFeaturesEXT heap{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT};
  VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
  VkPhysicalDevicePresentWaitFeaturesKHR present{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
  VkPhysicalDeviceVulkan13Features v13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
  VkPhysicalDeviceBufferDeviceAddressFeatures bda{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
  VkPhysicalDeviceMeshShaderFeaturesEXT mesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
  VkPhysicalDeviceRayQueryFeaturesKHR rq{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};

  heap.pNext = &shaderObj;
  timeline.pNext = &heap;
  present.pNext = &timeline;
  v13.pNext = &present;
  bda.pNext = &v13;
  mesh.pNext = &bda;
  rq.pNext = &mesh;

  VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
  f2.pNext = &rq;
  vkGetPhysicalDeviceFeatures2(physicalDevice_, &f2);

  caps_.shaderObject = shaderObj.shaderObject == VK_TRUE;
  caps_.descriptorHeap = heap.descriptorHeap == VK_TRUE;
  caps_.timelineSemaphore = timeline.timelineSemaphore == VK_TRUE;
  caps_.presentWait = present.presentWait == VK_TRUE;
  caps_.dynamicRendering = v13.dynamicRendering == VK_TRUE;
  caps_.bufferDeviceAddress = bda.bufferDeviceAddress == VK_TRUE;
  caps_.meshShader = mesh.meshShader == VK_TRUE;
  caps_.rayQuery = rq.rayQuery == VK_TRUE;

  if (!caps_.shaderObject || !caps_.descriptorHeap || !caps_.dynamicRendering) {
    throwVkError("GPU missing mandatory Burnhope features (shader object / descriptor heap / dynamic rendering)");
  }

  VkPhysicalDeviceDescriptorHeapPropertiesEXT heapProps{
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT};
  VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props2.pNext = &heapProps;
  vkGetPhysicalDeviceProperties2(physicalDevice_, &props2);
  caps_.heapProps = heapProps;
  caps_.maxPushDataSize = static_cast<uint32_t>(heapProps.maxPushDataSize);
}

void BurnhopeDevice::createTimelineSemaphore() {
  VkSemaphoreTypeCreateInfo typeInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
  typeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
  typeInfo.initialValue = 0;

  VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  semInfo.pNext = &typeInfo;
  if (vkCreateSemaphore(device_, &semInfo, nullptr, &frameTimeline_) != VK_SUCCESS) {
    throwVkError("frame timeline semaphore");
  }
}

void BurnhopeDevice::createGlobalSamplers() {
  VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.anisotropyEnable = VK_TRUE;
  info.maxAnisotropy = 16.0f;
  info.maxLod = VK_LOD_CLAMP_NONE;

  vkCreateSampler(device_, &info, nullptr, &linearRepeatSampler_);

  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  vkCreateSampler(device_, &info, nullptr, &linearClampSampler_);

  info.magFilter = VK_FILTER_NEAREST;
  info.minFilter = VK_FILTER_NEAREST;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.anisotropyEnable = VK_FALSE;
  vkCreateSampler(device_, &info, nullptr, &nearestClampSampler_);
}

void BurnhopeDevice::destroyGlobalSamplers() {
  auto destroy = [this](VkSampler& s) {
    if (s) {
      vkDestroySampler(device_, s, nullptr);
      s = VK_NULL_HANDLE;
    }
  };
  destroy(linearRepeatSampler_);
  destroy(linearClampSampler_);
  destroy(nearestClampSampler_);
}

void BurnhopeDevice::createCommandPool() {
  const auto indices = findPhysicalQueueFamilies();
  VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool.queueFamilyIndex = indices.graphicsFamily;
  pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &pool, nullptr, &commandPool_) != VK_SUCCESS) {
    throwVkError("command pool");
  }
}

void BurnhopeDevice::createSurface() {
  window_.createWindowSurface(instance_, &surface_);
}

VkDeviceSize BurnhopeDevice::descriptorSize(VkDescriptorType type) const {
  return vkext::get().getPhysicalDeviceDescriptorSizeEXT(physicalDevice_, type);
}

VkDeviceSize BurnhopeDevice::resourceHeapDescriptorStride() const {
  const VkDescriptorType types[] = {
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
  };
  VkDeviceSize stride = 0;
  for (VkDescriptorType type : types) {
    stride = std::max(stride, descriptorSize(type));
  }
  if (stride == 0) {
    throwVkError("BurnhopeDevice: invalid resource heap descriptor stride");
  }
  return stride;
}

VkDeviceAddress BurnhopeDevice::bufferDeviceAddress(VkBuffer buffer) const {
  VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
  info.buffer = buffer;
  return vkGetBufferDeviceAddress(device_, &info);
}

void BurnhopeDevice::createBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VmaAllocation& allocation) const {
  if (size == 0) {
    throwVkError("buffer size 0");
  }

  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  allocInfo.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
  if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
    allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                       VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }

  if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, &buffer, &allocation, nullptr) !=
      VK_SUCCESS) {
    throwVkError("vmaCreateBuffer");
  }
}

VkCommandBuffer BurnhopeDevice::beginSingleTimeCommands() const {
  VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  alloc.commandPool = commandPool_;
  alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc.commandBufferCount = 1;

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device_, &alloc, &cmd);

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  return cmd;
}

void BurnhopeDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) const {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer;
  vkQueueSubmit(graphicsQueue_, 1, &submit, uploadFence_);
  vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX);
  vkResetFences(device_, 1, &uploadFence_);
  vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void BurnhopeDevice::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
  VkCommandBuffer cmd = beginSingleTimeCommands();
  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(cmd, src, dst, 1, &region);
  endSingleTimeCommands(cmd);
}

void BurnhopeDevice::copyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    uint32_t layerCount) const {
  VkCommandBuffer cmd = beginSingleTimeCommands();
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layerCount};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  endSingleTimeCommands(cmd);
}

void BurnhopeDevice::createImageWithInfo(
    const VkImageCreateInfo& imageInfo,
    VkMemoryPropertyFlags,
    VkImage& image,
    VmaAllocation& allocation) const {
  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  if (vmaCreateImage(allocator_, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
    throwVkError("vmaCreateImage");
  }
}

void BurnhopeDevice::transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount) const {
  VkCommandBuffer cmd = beginSingleTimeCommands();

  VkImageMemoryBarrier2 barrier =
      makeImageBarrier2(image, oldLayout, newLayout, 0, 0, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels,
                        layerCount);

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    throwVkError("unsupported layout transition");
  }

  VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &barrier;
  vkCmdPipelineBarrier2(cmd, &dep);
  endSingleTimeCommands(cmd);
}

uint32_t BurnhopeDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties mem{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
  for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) && (mem.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  throwVkError("memory type");
}

VkFormat BurnhopeDevice::findSupportedFormat(
    std::span<const VkFormat> candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) const {
  for (VkFormat format : candidates) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &props);
    if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throwVkError("supported format");
}

bool BurnhopeDevice::isDeviceSuitable(VkPhysicalDevice device) const {
  const auto indices = findQueueFamilies(device);
  if (!indices.isComplete() || !checkDeviceExtensionSupport(device)) {
    return false;
  }
  const auto sc = querySwapChainSupport(device);
  if (sc.formats.empty() || sc.presentModes.empty()) {
    return false;
  }
  VkPhysicalDeviceFeatures features{};
  vkGetPhysicalDeviceFeatures(device, &features);
  return features.samplerAnisotropy == VK_TRUE;
}

void BurnhopeDevice::populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT& createInfo) const {
  createInfo = {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
}

void BurnhopeDevice::setupDebugMessenger() {
  if (!kEnableValidationLayers) {
    return;
  }
  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  populateDebugMessengerCreateInfo(createInfo);
  if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
    throwVkError("debug messenger");
  }
}

bool BurnhopeDevice::checkValidationLayerSupport() const {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());
  for (const char* required : kValidationLayers) {
    bool found = false;
    for (const auto& layer : layers) {
      if (std::strcmp(required, layer.layerName) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

std::vector<const char*> BurnhopeDevice::requiredInstanceExtensions() const {
  uint32_t count = 0;
  const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
  std::vector<const char*> extensions(sdlExtensions, sdlExtensions + count);
  if (kEnableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }
  return extensions;
}

void BurnhopeDevice::verifyRequiredInstanceExtensions() const {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
  std::unordered_set<std::string> names;
  for (const auto& ext : available) {
    names.insert(ext.extensionName);
  }
  for (const char* required : requiredInstanceExtensions()) {
    if (!names.contains(required)) {
      throwVkError(std::string{"missing instance extension: "} + required);
    }
  }
}

bool BurnhopeDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) const {
  uint32_t count = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> available(count);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());
  std::set<std::string> required(kDeviceExtensions.begin(), kDeviceExtensions.end());
  for (const auto& ext : available) {
    required.erase(ext.extensionName);
  }
  return required.empty();
}

QueueFamilyIndices BurnhopeDevice::findQueueFamilies(VkPhysicalDevice device) const {
  QueueFamilyIndices result;
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

  for (uint32_t i = 0; i < count; ++i) {
    const auto& q = families[i];
    if (q.queueCount == 0) {
      continue;
    }
    if ((q.queueFlags & VK_QUEUE_GRAPHICS_BIT) && result.graphicsFamily == UINT32_MAX) {
      result.graphicsFamily = i;
    }
    if ((q.queueFlags & VK_QUEUE_COMPUTE_BIT) && result.computeFamily == UINT32_MAX) {
      result.computeFamily = i;
    }
    if ((q.queueFlags & VK_QUEUE_TRANSFER_BIT) && result.transferFamily == UINT32_MAX) {
      result.transferFamily = i;
    }
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &present);
    if (present && result.presentFamily == UINT32_MAX) {
      result.presentFamily = i;
    }
    if (result.isComplete() && result.computeFamily != UINT32_MAX && result.transferFamily != UINT32_MAX) {
      break;
    }
  }
  return result;
}

SwapChainSupportDetails BurnhopeDevice::querySwapChainSupport(VkPhysicalDevice device) const {
  SwapChainSupportDetails details;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &details.capabilities);
  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
  if (formatCount) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, details.formats.data());
  }
  uint32_t presentCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentCount, nullptr);
  if (presentCount) {
    details.presentModes.resize(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &presentCount, details.presentModes.data());
  }
  return details;
}

} // namespace burnhope

namespace burnhope::vkext {

namespace {

Dispatch g_dispatch{};

template <typename Fn>
void loadDeviceProc(VkDevice device, const char* name, Fn& out) {
  out = reinterpret_cast<Fn>(vkGetDeviceProcAddr(device, name));
}

template <typename Fn>
void loadInstanceProc(VkInstance instance, const char* name, Fn& out) {
  out = reinterpret_cast<Fn>(vkGetInstanceProcAddr(instance, name));
}

} // namespace

void loadInstance(VkInstance instance) {
  loadInstanceProc(instance, "vkGetPhysicalDeviceDescriptorSizeEXT",
                   g_dispatch.getPhysicalDeviceDescriptorSizeEXT);
}

void loadDevice(VkDevice device) {
  loadDeviceProc(device, "vkWriteResourceDescriptorsEXT", g_dispatch.writeResourceDescriptorsEXT);
  loadDeviceProc(device, "vkWriteSamplerDescriptorsEXT", g_dispatch.writeSamplerDescriptorsEXT);
  loadDeviceProc(device, "vkCmdBindResourceHeapEXT", g_dispatch.cmdBindResourceHeapEXT);
  loadDeviceProc(device, "vkCmdBindSamplerHeapEXT", g_dispatch.cmdBindSamplerHeapEXT);
  loadDeviceProc(device, "vkCreateShadersEXT", g_dispatch.createShadersEXT);
  loadDeviceProc(device, "vkDestroyShaderEXT", g_dispatch.destroyShaderEXT);
  loadDeviceProc(device, "vkCmdBindShadersEXT", g_dispatch.cmdBindShadersEXT);
  loadDeviceProc(device, "vkCmdPushDataEXT", g_dispatch.cmdPushDataEXT);
}

const Dispatch& get() noexcept { return g_dispatch; }

} // namespace burnhope::vkext
