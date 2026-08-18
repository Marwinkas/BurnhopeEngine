#pragma once
#include "Window.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <vk_mem_alloc.h>
namespace burnhope
{
  struct SwapChainSupportDetails
  {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
  };
  struct QueueFamilyIndices
  {
    uint32_t graphicsFamily;
    uint32_t presentFamily;
    bool graphicsFamilyHasValue = false;
    bool presentFamilyHasValue = false;
    bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
  };
  class BurnhopeDevice
  {
  public:
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
    BurnhopeDevice(BurnhopeWindow &window);
    ~BurnhopeDevice();
    BurnhopeDevice(const BurnhopeDevice &) = delete;
    BurnhopeDevice &operator=(const BurnhopeDevice &) = delete;
    BurnhopeDevice(BurnhopeDevice &&) = delete;
    BurnhopeDevice &operator=(BurnhopeDevice &&) = delete;
    VkCommandPool getCommandPool() { return commandPool; }
    VkDevice device() { return device_; }
    VkSurfaceKHR surface() { return surface_; }
    VkQueue graphicsQueue() { return graphicsQueue_; }
    VkQueue presentQueue() { return presentQueue_; }
    VkInstance getInstance() { return instance; }
    VkPhysicalDevice getPhysicalDevice() { return physicalDevice; }
    uint32_t getGraphicsQueueFamily() { return findPhysicalQueueFamilies().graphicsFamily; }
    SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }
    VkFormat findSupportedFormat(
        const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    void createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer &buffer,
        VmaAllocation &allocation);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void copyBufferToImage(
        VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);
    void createImageWithInfo(
        const VkImageCreateInfo &imageInfo,
        VkMemoryPropertyFlags properties,
        VkImage &image,
        VmaAllocation &allocation);
    void transitionImageLayout(
        VkImage image,
        VkFormat format,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        uint32_t mipLevels = 1,
        uint32_t layerCount = 1);
        VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer);
    VkPipelineCache getPipelineCache() const { return pipelineCache; }
    VkSampler getLinearRepeatSampler() const { return linearRepeatSampler; }
    VmaAllocator getAllocator() const { return allocator; }
    VkSampler getLinearClampSampler() const { return linearClampSampler; }
    VkSampler getNearestClampSampler() const { return nearestClampSampler; }
    VkPhysicalDeviceProperties properties;
  private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createGlobalSamplers();
    void destroyGlobalSamplers();
    void createPipelineCache();
    void savePipelineCache();
    bool isDeviceSuitable(VkPhysicalDevice device);
    std::vector<const char *> getRequiredExtensions();
    bool checkValidationLayerSupport();
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
    void hasSdlRequiredInstanceExtensions();
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    BurnhopeWindow &window;
    VkCommandPool commandPool;
    // Guards single-time command recording/submission and the persistent
    // buffer below: asset loading (models/textures/UI init) can happen from
    // background loader threads while the main thread is also uploading via
    // the same pool/queue, which is illegal per the Vulkan spec without
    // external synchronization. Off the render hot path (load-time only).
    std::mutex m_SingleTimeCommandMutex;
    // Reused via vkResetCommandBuffer rather than allocated/freed per call.
    // Rapid alloc+free of a VkCommandBuffer from the same pool was observed
    // to trip an internal validation-layer bug (stale per-handle tracking
    // state reused at the same address); resetting one persistent buffer
    // sidesteps that and is also cheaper for load-time uploads.
    VkCommandBuffer m_SingleTimeCommandBuffer = VK_NULL_HANDLE;
    VkDevice device_;
    VkSurfaceKHR surface_;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkSampler linearRepeatSampler = VK_NULL_HANDLE;
    VkSampler linearClampSampler = VK_NULL_HANDLE;
    VmaAllocator allocator;
    VkSampler nearestClampSampler = VK_NULL_HANDLE;
    VkQueue graphicsQueue_;
    VkQueue presentQueue_;
    const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
        VK_EXT_MESH_SHADER_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME
    };
  };
}
