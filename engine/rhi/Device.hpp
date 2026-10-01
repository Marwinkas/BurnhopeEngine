#pragma once

#include "rhi/DeviceCaps.hpp"
#include "platform/Window.hpp"

#include <volk.h>
#include <vk_mem_alloc.h>

namespace burnhope {

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DeviceCaps caps{};
    VkPhysicalDeviceDescriptorHeapPropertiesEXT heapProps{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
    };
    bool validation = false;
};

[[nodiscard]] bool deviceCreate(Device& d, Window& window);
void deviceDestroy(Device& d);
void deviceDumpCaps(const Device& d);

} // namespace burnhope
