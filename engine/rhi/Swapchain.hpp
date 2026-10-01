#pragma once

#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kFramesInFlight = 2;

struct Swapchain {
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent{};
    uint32_t imageCount = 0;
    VkImage images[8]{};
    VkImageView views[8]{};

    VkCommandBuffer cmd[kFramesInFlight]{};
    VkSemaphore acquireSem[kFramesInFlight]{};
    VkSemaphore renderSem[8]{};
    VkFence flightFence[kFramesInFlight]{};
    uint64_t presentId = 0;
    uint32_t frame = 0;
};

struct FrameContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    uint32_t flight = 0;
};

[[nodiscard]] bool swapchainCreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h);
void swapchainDestroy(Swapchain& sc, Device& d);
[[nodiscard]] bool swapchainRecreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h);

[[nodiscard]] bool swapchainBegin(Swapchain& sc, Device& d, FrameContext& fc);
void swapchainToPresent(Swapchain& sc, const FrameContext& fc);
[[nodiscard]] bool swapchainSubmitPresent(Swapchain& sc, Device& d, FrameContext& fc);

} // namespace burnhope
