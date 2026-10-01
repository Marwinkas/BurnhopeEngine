#include "rhi/Swapchain.hpp"
#include "rhi/Barrier.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <vector>

namespace burnhope {
namespace {

VkSurfaceFormatKHR pickFormat(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &n, fmts.data());
    for (const auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return fmts[0];
}

VkPresentModeKHR pickPresent(VkPhysicalDevice pd, VkSurfaceKHR surface) {
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &n, modes.data());
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

bool swapchainCreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h) {
    return swapchainCreate(sc, d, d.surface, w, h);
}

bool swapchainCreate(Swapchain& sc, Device& d, VkSurfaceKHR surface, uint32_t w, uint32_t h) {
    sc.surface = surface;
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physical, surface, &caps);

    const VkSurfaceFormatKHR fmt = pickFormat(d.physical, surface);
    sc.format = fmt.format;
    sc.colorSpace = fmt.colorSpace;

    if (caps.currentExtent.width != UINT32_MAX) {
        sc.extent = caps.currentExtent;
    } else {
        sc.extent.width = std::clamp(w, caps.minImageExtent.width, caps.maxImageExtent.width);
        sc.extent.height = std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (sc.extent.width == 0 || sc.extent.height == 0) {
        return false;
    }

    uint32_t imgCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) {
        imgCount = caps.maxImageCount;
    }

    const uint32_t families[] = {d.caps.graphicsFamily, d.caps.presentFamily};
    const bool exclusive = d.caps.graphicsFamily == d.caps.presentFamily;

    VkSwapchainCreateInfoKHR ci{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imgCount,
        .imageFormat = sc.format,
        .imageColorSpace = sc.colorSpace,
        .imageExtent = sc.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = exclusive ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = exclusive ? 0u : 2u,
        .pQueueFamilyIndices = exclusive ? nullptr : families,
        .preTransform = caps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = pickPresent(d.physical, surface),
        .clipped = VK_TRUE,
    };

    if (vkCreateSwapchainKHR(d.device, &ci, nullptr, &sc.handle) != VK_SUCCESS) {
        spdlog::error("vkCreateSwapchainKHR failed");
        return false;
    }

    vkGetSwapchainImagesKHR(d.device, sc.handle, &sc.imageCount, nullptr);
    if (sc.imageCount > 8) {
        spdlog::error("swapchain imageCount {}", sc.imageCount);
        return false;
    }
    vkGetSwapchainImagesKHR(d.device, sc.handle, &sc.imageCount, sc.images);

    for (uint32_t i = 0; i < sc.imageCount; ++i) {
        VkImageViewCreateInfo vi{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = sc.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = sc.format,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        if (vkCreateImageView(d.device, &vi, nullptr, &sc.views[i]) != VK_SUCCESS) {
            return false;
        }
    }

    VkCommandBufferAllocateInfo ai{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = d.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = kFramesInFlight,
    };
    if (vkAllocateCommandBuffers(d.device, &ai, sc.cmd) != VK_SUCCESS) {
        return false;
    }

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(d.device, &si, nullptr, &sc.acquireSem[i]) != VK_SUCCESS) {
            return false;
        }
        if (vkCreateFence(d.device, &fi, nullptr, &sc.flightFence[i]) != VK_SUCCESS) {
            return false;
        }
    }
    for (uint32_t i = 0; i < sc.imageCount; ++i) {
        if (vkCreateSemaphore(d.device, &si, nullptr, &sc.renderSem[i]) != VK_SUCCESS) {
            return false;
        }
    }
    sc.frame = 0;
    sc.presentId = 0;
    return true;
}

void swapchainDestroy(Swapchain& sc, Device& d) {
    if (d.device == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(d.device);
    if (sc.cmd[0] != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(d.device, d.commandPool, kFramesInFlight, sc.cmd);
    }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (sc.acquireSem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(d.device, sc.acquireSem[i], nullptr);
        }
        if (sc.flightFence[i] != VK_NULL_HANDLE) {
            vkDestroyFence(d.device, sc.flightFence[i], nullptr);
        }
        sc.acquireSem[i] = VK_NULL_HANDLE;
        sc.flightFence[i] = VK_NULL_HANDLE;
        sc.cmd[i] = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < sc.imageCount; ++i) {
        if (sc.renderSem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(d.device, sc.renderSem[i], nullptr);
            sc.renderSem[i] = VK_NULL_HANDLE;
        }
        if (sc.views[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(d.device, sc.views[i], nullptr);
            sc.views[i] = VK_NULL_HANDLE;
        }
        sc.images[i] = VK_NULL_HANDLE;
    }
    if (sc.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(d.device, sc.handle, nullptr);
        sc.handle = VK_NULL_HANDLE;
    }
    sc.imageCount = 0;
}

bool swapchainRecreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h) {
    const VkSurfaceKHR surface = sc.surface != VK_NULL_HANDLE ? sc.surface : d.surface;
    swapchainDestroy(sc, d);
    return swapchainCreate(sc, d, surface, w, h);
}

bool swapchainBegin(Swapchain& sc, Device& d, FrameContext& fc) {
    const uint32_t f = sc.frame;
    vkWaitForFences(d.device, 1, &sc.flightFence[f], VK_TRUE, UINT64_MAX);
    vkResetFences(d.device, 1, &sc.flightFence[f]);

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(d.device, sc.handle, UINT64_MAX, sc.acquireSem[f], VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        spdlog::error("vkAcquireNextImageKHR {}", static_cast<int>(acq));
        return false;
    }

    vkResetCommandBuffer(sc.cmd[f], 0);
    VkCommandBufferBeginInfo bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(sc.cmd[f], &bi);

    fc.cmd = sc.cmd[f];
    fc.imageIndex = imageIndex;
    fc.flight = f;
    return true;
}

void swapchainToPresent(Swapchain& sc, const FrameContext& fc) {
    rhiImageBarrier(fc.cmd, sc.images[fc.imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
}

bool swapchainSubmitPresent(Swapchain& sc, Device& d, FrameContext& fc) {
    vkEndCommandBuffer(fc.cmd);

    VkSemaphoreSubmitInfo waitSem{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sc.acquireSem[fc.flight],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    VkSemaphoreSubmitInfo sigSem{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sc.renderSem[fc.imageIndex],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    VkCommandBufferSubmitInfo cbsi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = fc.cmd,
    };
    VkSubmitInfo2 si{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitSem,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbsi,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &sigSem,
    };
    const VkResult sub = vkQueueSubmit2(d.graphicsQueue, 1, &si, sc.flightFence[fc.flight]);
    if (sub != VK_SUCCESS) {
        spdlog::error("vkQueueSubmit2 failed: {}", static_cast<int>(sub));
        return false;
    }

    sc.presentId += 1;
    uint64_t pid = sc.presentId;
    VkPresentIdKHR presentIdInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .swapchainCount = 1,
        .pPresentIds = &pid,
    };

    VkPresentInfoKHR pi{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = (d.caps.presentId && d.caps.presentWait) ? &presentIdInfo : nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &sc.renderSem[fc.imageIndex],
        .swapchainCount = 1,
        .pSwapchains = &sc.handle,
        .pImageIndices = &fc.imageIndex,
    };
    VkResult pres = vkQueuePresentKHR(d.presentQueue, &pi);
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (pres != VK_SUCCESS) {
        spdlog::error("vkQueuePresentKHR {}", static_cast<int>(pres));
        return false;
    }

    if (d.caps.presentWait && vkWaitForPresentKHR != nullptr) {
        vkWaitForPresentKHR(d.device, sc.handle, pid, UINT64_MAX);
    }

    sc.frame = (sc.frame + 1) % kFramesInFlight;
    return true;
}

} // namespace burnhope
