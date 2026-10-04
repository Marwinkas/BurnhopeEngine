#pragma once

#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstdint>

namespace burnhope {

// Разовая работа видеокарты вне буфера кадра. Свой пул, буфер и fence.
// Модуль — структура и функции, не базовый класс.
struct GpuTask {
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    // 0 свободен, 1 записан, 2 в очереди.
    uint8_t stage = 0;
};

// Первый вызов создаёт пул, буфер и fence. Следующий сбрасывает пул и открывает буфер.
[[nodiscard]] inline bool gpuTaskBegin(GpuTask& task, Device& device) {
    if (device.device == VK_NULL_HANDLE || deviceLost(device)) {
        return false;
    }
    if (task.pool == VK_NULL_HANDLE) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = device.caps.graphicsFamily;
        if (vkCreateCommandPool(device.device, &poolInfo, nullptr, &task.pool) != VK_SUCCESS) {
            return false;
        }
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = task.pool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device.device, &alloc, &task.cmd) != VK_SUCCESS) {
            return false;
        }
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        if (vkCreateFence(device.device, &fenceInfo, nullptr, &task.fence) != VK_SUCCESS) {
            return false;
        }
    } else if (vkResetCommandPool(device.device, task.pool, 0) != VK_SUCCESS) {
        return false;
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(task.cmd, &begin) != VK_SUCCESS) {
        return false;
    }
    task.stage = 0;
    return true;
}

[[nodiscard]] inline bool gpuTaskEnd(GpuTask& task) {
    if (task.cmd == VK_NULL_HANDLE || vkEndCommandBuffer(task.cmd) != VK_SUCCESS) {
        return false;
    }
    task.stage = 1;
    return true;
}

// Сабмит на graphics. Fence с прошлого раза сбрасывается, если уже сигналит.
[[nodiscard]] inline bool gpuTaskSubmit(GpuTask& task, Device& device) {
    if (task.stage != 1 || task.cmd == VK_NULL_HANDLE || task.fence == VK_NULL_HANDLE || deviceLost(device)) {
        return false;
    }
    if (vkGetFenceStatus(device.device, task.fence) == VK_SUCCESS) {
        vkResetFences(device.device, 1, &task.fence);
    }
    VkCommandBufferSubmitInfo cb{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = task.cmd,
    };
    VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cb,
    };
    std::unique_lock<std::mutex> lock(device.queueMutex);
    const VkResult result = vkQueueSubmit2(device.graphicsQueue, 1, &submit, task.fence);
    lock.unlock();
    if (result != VK_SUCCESS) {
        if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            deviceMarkLost(device, "gpu task", static_cast<int32_t>(result));
        }
        return false;
    }
    task.stage = 2;
    return true;
}

// Готово только после сабмита, когда fence сигналит. Несигналенный fence не роняет процесс.
[[nodiscard]] inline bool gpuTaskDone(const GpuTask& task, const Device& device) {
    if (task.stage != 2 || task.fence == VK_NULL_HANDLE || device.device == VK_NULL_HANDLE) {
        return false;
    }
    return vkGetFenceStatus(device.device, task.fence) == VK_SUCCESS;
}

inline void gpuTaskDestroy(GpuTask& task, Device& device) {
    if (task.fence != VK_NULL_HANDLE && device.device != VK_NULL_HANDLE && !deviceLost(device)) {
        vkWaitForFences(device.device, 1, &task.fence, VK_TRUE, 200'000'000ull);
    }
    if (task.pool != VK_NULL_HANDLE && task.cmd != VK_NULL_HANDLE && device.device != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(device.device, task.pool, 1, &task.cmd);
    }
    if (task.pool != VK_NULL_HANDLE && device.device != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device.device, task.pool, nullptr);
    }
    if (task.fence != VK_NULL_HANDLE && device.device != VK_NULL_HANDLE) {
        vkDestroyFence(device.device, task.fence, nullptr);
    }
    task = {};
}

// Группы compute: (pixels + group - 1) / group. Ноль пикселей даёт ноль групп.
[[nodiscard]] inline uint32_t passGroups(uint32_t pixels, uint32_t group = 8u) {
    if (group == 0u) {
        return 0u;
    }
    return (pixels + group - 1u) / group;
}

[[nodiscard]] inline VkDeviceSize alignUp(VkDeviceSize size, VkDeviceSize alignment = 256) {
    if (alignment == 0) {
        return size;
    }
    return (size + alignment - 1) & ~(alignment - 1);
}

inline void dispatch1D(VkCommandBuffer cmd, uint32_t count, uint32_t group = 64u) {
    vkCmdDispatch(cmd, passGroups(count, group), 1u, 1u);
}

inline void dispatch2D(VkCommandBuffer cmd, VkExtent2D extent, uint32_t group = 8u) {
    vkCmdDispatch(cmd, passGroups(extent.width, group), passGroups(extent.height, group), 1u);
}

inline void dispatchCompute2D(VkCommandBuffer cmd, const ShaderExt& shader, VkExtent2D extent, uint32_t group = 8u) {
    cmdBindCompute(cmd, shader.handle);
    dispatch2D(cmd, extent, group);
}

[[nodiscard]] inline VkExtent2D mipExtent(VkExtent2D extent, uint32_t mip) {
    const uint32_t width = extent.width >> mip;
    const uint32_t height = extent.height >> mip;
    return {width == 0u ? 1u : width, height == 0u ? 1u : height};
}

} // namespace burnhope
