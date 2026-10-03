#pragma once

#include "rhi/DeviceCaps.hpp"
#include "platform/Window.hpp"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <mutex>

namespace burnhope {

struct Device {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandPool transferPool = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DeviceCaps caps{};
    VkPhysicalDeviceDescriptorHeapPropertiesEXT heapProps{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT,
    };
    bool validation = false;

    // Одна и та же блокировка на: (1) vkQueueSubmit2/vkQueuePresentKHR на общей VkQueue
    // (graphicsQueue/presentQueue/transferQueue — одни на все окна, спецификация требует
    // внешней синхронизации при вызове из разных потоков), и (2) d.commandPool/d.transferPool
    // (общий разовый пул для загрузки текстур/шрифтов — ImageFile.cpp, Font.cpp; сам пул не
    // потокобезопасен для allocate/free из разных потоков). Один мьютекс, а не два: разовая
    // загрузка текстуры сама делает submit на ту же очередь, что и кадр — раздельные мьютексы
    // не дали бы взаимного исключения между ними, где оно и нужно. Держит параллельную запись
    // кадра разных окон (core/host/Host.cpp, job system).
    std::mutex queueMutex;
};

[[nodiscard]] bool deviceCreate(Device& d, Window& window);
[[nodiscard]] bool deviceCreateSurface(Device& d, Window& window, VkSurfaceKHR& out);
void deviceDestroy(Device& d);
void deviceDumpCaps(const Device& d);
void deviceName(Device& d, uint64_t handle, VkObjectType type, const char* name);
void rhiCaptureToggle();

} // namespace burnhope
