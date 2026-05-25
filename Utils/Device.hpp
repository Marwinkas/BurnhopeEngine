#pragma once

#include "Window.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace burnhope {

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities{};
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

struct QueueFamilyIndices {
  uint32_t graphicsFamily{UINT32_MAX};
  uint32_t presentFamily{UINT32_MAX};
  uint32_t computeFamily{UINT32_MAX};
  uint32_t transferFamily{UINT32_MAX};

  [[nodiscard]] bool isComplete() const noexcept {
    return graphicsFamily != UINT32_MAX && presentFamily != UINT32_MAX;
  }
};

/** Enabled Vulkan 1.4 + mandatory extensions (queried once at init). */
struct DeviceCaps final {
  bool shaderObject{false};
  bool descriptorHeap{false};
  bool timelineSemaphore{false};
  bool presentWait{false};
  bool dynamicRendering{false};
  bool bufferDeviceAddress{false};
  bool meshShader{false};
  bool rayQuery{false};
  bool cooperativeMatrix{false};
  VkPhysicalDeviceProperties properties{};
  VkPhysicalDeviceDescriptorHeapPropertiesEXT heapProps{};
  uint32_t maxPushDataSize{128};
};

class BurnhopeDevice final {
public:
#if defined(BURNHOPE_VULKAN_VALIDATION) && BURNHOPE_VULKAN_VALIDATION
  static constexpr bool kEnableValidationLayers = true;
#elif !defined(NDEBUG)
  static constexpr bool kEnableValidationLayers = true;
#else
  static constexpr bool kEnableValidationLayers = false;
#endif

  explicit BurnhopeDevice(BurnhopeWindow& window);
  ~BurnhopeDevice();

  BurnhopeDevice(const BurnhopeDevice&) = delete;
  BurnhopeDevice& operator=(const BurnhopeDevice&) = delete;
  BurnhopeDevice(BurnhopeDevice&&) = delete;
  BurnhopeDevice& operator=(BurnhopeDevice&&) = delete;

  [[nodiscard]] VkCommandPool commandPool() const noexcept { return commandPool_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] VkSurfaceKHR surface() const noexcept { return surface_; }
  [[nodiscard]] VkQueue graphicsQueue() const noexcept { return graphicsQueue_; }
  [[nodiscard]] VkQueue presentQueue() const noexcept { return presentQueue_; }
  [[nodiscard]] VkQueue computeQueue() const noexcept { return computeQueue_; }
  [[nodiscard]] VkQueue transferQueue() const noexcept { return transferQueue_; }
  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] const DeviceCaps& caps() const noexcept { return caps_; }
  [[nodiscard]] VmaAllocator allocator() const noexcept { return allocator_; }

  [[nodiscard]] uint32_t graphicsQueueFamily() const {
    return findPhysicalQueueFamilies().graphicsFamily;
  }

  [[nodiscard]] SwapChainSupportDetails swapChainSupport() const {
    return querySwapChainSupport(physicalDevice_);
  }

  [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

  [[nodiscard]] QueueFamilyIndices findPhysicalQueueFamilies() const {
    return findQueueFamilies(physicalDevice_);
  }

  [[nodiscard]] VkFormat findSupportedFormat(
      std::span<const VkFormat> candidates,
      VkImageTiling tiling,
      VkFormatFeatureFlags features) const;

  template <class Container>
    requires requires(const Container& c) { c.data(); c.size(); }
  [[nodiscard]] VkFormat findSupportedFormat(
      const Container& candidates,
      VkImageTiling tiling,
      VkFormatFeatureFlags features) const {
    return findSupportedFormat(
        std::span<const VkFormat>{candidates.data(), candidates.size()}, tiling, features);
  }

  [[nodiscard]] VkFormat findSupportedFormat(
      std::initializer_list<VkFormat> candidates,
      VkImageTiling tiling,
      VkFormatFeatureFlags features) const {
    return findSupportedFormat(std::span<const VkFormat>{candidates.begin(), candidates.size()},
                               tiling, features);
  }

  void createBuffer(
      VkDeviceSize size,
      VkBufferUsageFlags usage,
      VkMemoryPropertyFlags properties,
      VkBuffer& buffer,
      VmaAllocation& allocation) const;

  [[nodiscard]] VkDeviceAddress bufferDeviceAddress(VkBuffer buffer) const;

  [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
  void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

  void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
  void copyBufferToImage(
      VkBuffer buffer,
      VkImage image,
      uint32_t width,
      uint32_t height,
      uint32_t layerCount = 1) const;

  void createImageWithInfo(
      const VkImageCreateInfo& imageInfo,
      VkMemoryPropertyFlags properties,
      VkImage& image,
      VmaAllocation& allocation) const;

  void transitionImageLayout(
      VkImage image,
      VkFormat format,
      VkImageLayout oldLayout,
      VkImageLayout newLayout,
      uint32_t mipLevels = 1,
      uint32_t layerCount = 1) const;

  [[nodiscard]] VkSampler linearRepeatSampler() const noexcept { return linearRepeatSampler_; }
  [[nodiscard]] VkSampler linearClampSampler() const noexcept { return linearClampSampler_; }
  [[nodiscard]] VkSampler nearestClampSampler() const noexcept { return nearestClampSampler_; }

  /** Timeline semaphore for frame graph / upload (no binary semaphores in core path). */
  [[nodiscard]] VkSemaphore frameTimelineSemaphore() const noexcept { return frameTimeline_; }
  [[nodiscard]] uint64_t& frameTimelineValue() noexcept { return frameTimelineValue_; }

  [[nodiscard]] VkDeviceSize descriptorSize(VkDescriptorType type) const;
  /** Max vkGetPhysicalDeviceDescriptorSizeEXT across resource heap types (must match ShaderLibrary heapIndexStride). */
  [[nodiscard]] VkDeviceSize resourceHeapDescriptorStride() const;

  // Transitional aliases (Render migration) — remove when call sites updated.
  [[nodiscard]] VkCommandPool getCommandPool() const noexcept { return commandPool(); }
  [[nodiscard]] VmaAllocator getAllocator() const noexcept { return allocator(); }
  [[nodiscard]] VkInstance getInstance() const noexcept { return instance(); }
  [[nodiscard]] VkPhysicalDevice getPhysicalDevice() const noexcept { return physicalDevice(); }
  [[nodiscard]] VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) const {
    return bufferDeviceAddress(buffer);
  }
  [[nodiscard]] VkSampler getLinearRepeatSampler() const noexcept { return linearRepeatSampler(); }
  [[nodiscard]] VkSampler getLinearClampSampler() const noexcept { return linearClampSampler(); }
  [[nodiscard]] VkSampler getNearestClampSampler() const noexcept { return nearestClampSampler(); }
  [[nodiscard]] SwapChainSupportDetails getSwapChainSupport() const { return swapChainSupport(); }
  VkPhysicalDeviceProperties properties{};

private:
  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createCommandPool();
  void createGlobalSamplers();
  void destroyGlobalSamplers();
  void createTimelineSemaphore();
  void queryCaps();

  [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
  [[nodiscard]] std::vector<const char*> requiredInstanceExtensions() const;
  [[nodiscard]] bool checkValidationLayerSupport() const;
  [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) const;
  void verifyRequiredInstanceExtensions() const;
  [[nodiscard]] bool checkDeviceExtensionSupport(VkPhysicalDevice device) const;
  [[nodiscard]] SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device) const;

  static constexpr std::array kValidationLayers = {"VK_LAYER_KHRONOS_validation"};

  static constexpr std::array kDeviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME,
      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
      VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
      VK_KHR_RAY_QUERY_EXTENSION_NAME,
      VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
      VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
      VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
      VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME,
      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
      VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
      VK_KHR_PRESENT_ID_EXTENSION_NAME,
      VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
      VK_KHR_PIPELINE_BINARY_EXTENSION_NAME,
      VK_EXT_MESH_SHADER_EXTENSION_NAME,
      VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
      VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
      VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME,
      VK_KHR_SHADER_UNTYPED_POINTERS_EXTENSION_NAME,
      VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME,
      VK_KHR_SHADER_SUBGROUP_ROTATE_EXTENSION_NAME,
      VK_KHR_SHADER_EXPECT_ASSUME_EXTENSION_NAME,
      VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_EXTENSION_NAME,
      VK_KHR_WORKGROUP_MEMORY_EXPLICIT_LAYOUT_EXTENSION_NAME,
      VK_EXT_SHADER_DEMOTE_TO_HELPER_INVOCATION_EXTENSION_NAME,
      VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME,
      VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME,
  };

  VkInstance instance_{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debugMessenger_{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
  BurnhopeWindow& window_;
  VkCommandPool commandPool_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VmaAllocator allocator_{VK_NULL_HANDLE};

  VkSampler linearRepeatSampler_{VK_NULL_HANDLE};
  VkSampler linearClampSampler_{VK_NULL_HANDLE};
  VkSampler nearestClampSampler_{VK_NULL_HANDLE};

  VkQueue graphicsQueue_{VK_NULL_HANDLE};
  VkQueue presentQueue_{VK_NULL_HANDLE};
  VkQueue computeQueue_{VK_NULL_HANDLE};
  VkQueue transferQueue_{VK_NULL_HANDLE};

  VkSemaphore frameTimeline_{VK_NULL_HANDLE};
  uint64_t frameTimelineValue_{0};

  mutable VkFence uploadFence_{VK_NULL_HANDLE};
  DeviceCaps caps_{};
};

} // namespace burnhope
