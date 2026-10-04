#include "rhi/Swapchain.hpp"
#include "rhi/Barrier.hpp"
#include "platform/Trace.hpp"

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

VkPresentModeKHR askedMode(Present present) {
    if (present == Present::Mailbox) {
        return VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if (present == Present::Immediate) {
        return VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
    if (present == Present::Relaxed) {
        return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

const char* presentName(VkPresentModeKHR mode) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
        return "mailbox";
    }
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
        return "immediate";
    }
    if (mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR) {
        return "relaxed";
    }
    return "vsync";
}

VkPresentModeKHR pickPresent(VkPhysicalDevice pd, VkSurfaceKHR surface, Present present) {
    const VkPresentModeKHR want = askedMode(present);
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &n, modes.data());
    for (auto m : modes) {
        if (m == want) {
            return m;
        }
    }
    if (want != VK_PRESENT_MODE_FIFO_KHR) {
        spdlog::info("present {} unavailable, vsync", presentName(want));
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR pickAlpha(const VkSurfaceCapabilitiesKHR& caps, bool transparent) {
    if (!transparent) {
        if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }
    } else {
        if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        }
        if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        }
        if ((caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) != 0) {
            return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

} // namespace

bool swapchainCreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h) {
    return swapchainCreate(sc, d, d.surface, w, h);
}

bool swapchainCreate(Swapchain& sc, Device& d, VkSurfaceKHR surface, uint32_t w, uint32_t h, const FrameDesc& frame, VkSwapchainKHR retired) {
    sc.surface = surface;
    sc.asked = frame;
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
        .compositeAlpha = pickAlpha(caps, frame.transparent),
        .presentMode = pickPresent(d.physical, surface, frame.present),
        .clipped = frame.clipped ? VK_TRUE : VK_FALSE,
        .oldSwapchain = retired,
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

    const VkCommandPoolCreateInfo poolCi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = d.caps.graphicsFamily,
    };
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateCommandPool(d.device, &poolCi, nullptr, &sc.pool[i]) != VK_SUCCESS) {
            return false;
        }
        VkCommandBufferAllocateInfo ai{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = sc.pool[i],
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 2,
        };
        VkCommandBuffer pair[2]{};
        if (vkAllocateCommandBuffers(d.device, &ai, pair) != VK_SUCCESS) {
            return false;
        }
        sc.cmd[i] = pair[0];
        sc.uiCmd[i] = pair[1];
    }

    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (vkCreateSemaphore(d.device, &si, nullptr, &sc.acquireSem[i]) != VK_SUCCESS
            || vkCreateSemaphore(d.device, &si, nullptr, &sc.uiSem[i]) != VK_SUCCESS) {
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
    if (d.caps.timelineSemaphore) {
        VkSemaphoreTypeCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo orderInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        orderInfo.pNext = &timeline;
        if (vkCreateSemaphore(d.device, &orderInfo, nullptr, &sc.orderSem) != VK_SUCCESS) {
            sc.orderSem = VK_NULL_HANDLE;
        }
    }
    sc.frame = 0;
    sc.orderValue = 0;
    sc.presentId = 0;
    return true;
}

void swapchainDestroy(Swapchain& sc, Device& d) {
    if (d.device == VK_NULL_HANDLE) {
        return;
    }
    if (!deviceLost(d)) {
        for (uint32_t i = 0; i < kFramesInFlight; ++i) {
            if (sc.flightFence[i] != VK_NULL_HANDLE) {
                const VkResult waited = vkWaitForFences(d.device, 1, &sc.flightFence[i], VK_TRUE, 200'000'000ull);
                if (waited == VK_TIMEOUT || waited == VK_ERROR_DEVICE_LOST) {
                    deviceMarkLost(d, "swapchain fence", static_cast<int32_t>(waited));
                    break;
                }
            }
        }
    }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (sc.pool[i] != VK_NULL_HANDLE && (sc.cmd[i] != VK_NULL_HANDLE || sc.uiCmd[i] != VK_NULL_HANDLE)) {
            VkCommandBuffer pair[2] = {sc.cmd[i], sc.uiCmd[i]};
            uint32_t n = 0;
            VkCommandBuffer live[2]{};
            if (pair[0] != VK_NULL_HANDLE) {
                live[n++] = pair[0];
            }
            if (pair[1] != VK_NULL_HANDLE) {
                live[n++] = pair[1];
            }
            if (n > 0) {
                vkFreeCommandBuffers(d.device, sc.pool[i], n, live);
            }
        }
        if (sc.pool[i] != VK_NULL_HANDLE) {
            vkDestroyCommandPool(d.device, sc.pool[i], nullptr);
            sc.pool[i] = VK_NULL_HANDLE;
        }
        sc.cmd[i] = VK_NULL_HANDLE;
        sc.uiCmd[i] = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (sc.acquireSem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(d.device, sc.acquireSem[i], nullptr);
        }
        if (sc.uiSem[i] != VK_NULL_HANDLE) {
            vkDestroySemaphore(d.device, sc.uiSem[i], nullptr);
        }
        if (sc.flightFence[i] != VK_NULL_HANDLE) {
            vkDestroyFence(d.device, sc.flightFence[i], nullptr);
        }
        sc.acquireSem[i] = VK_NULL_HANDLE;
        sc.uiSem[i] = VK_NULL_HANDLE;
        sc.flightFence[i] = VK_NULL_HANDLE;
    }
    if (sc.orderSem != VK_NULL_HANDLE) {
        vkDestroySemaphore(d.device, sc.orderSem, nullptr);
        sc.orderSem = VK_NULL_HANDLE;
        sc.orderValue = 0;
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
        sc.presented[i] = 0;
        sc.inColor[i] = 0;
    }
    if (sc.handle != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(d.device, sc.handle, nullptr);
        sc.handle = VK_NULL_HANDLE;
    }
    sc.imageCount = 0;
}

bool swapchainRecreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h) {
    const VkSurfaceKHR surface = sc.surface != VK_NULL_HANDLE ? sc.surface : d.surface;
    const FrameDesc frame = sc.asked;
    const VkSwapchainKHR retired = sc.handle;
    sc.handle = VK_NULL_HANDLE;
    swapchainDestroy(sc, d);
    const bool ok = swapchainCreate(sc, d, surface, w, h, frame, retired);
    if (retired != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(d.device, retired, nullptr);
    }
    return ok;
}

bool swapchainBegin(Swapchain& sc, Device& d, FrameContext& fc) {
    BH_ZONE;
    if (deviceLost(d)) {
        spdlog::error("RHI acquire skipped, device lost");
        return false;
    }
    const uint32_t f = sc.frame;
    const VkResult waited = vkWaitForFences(d.device, 1, &sc.flightFence[f], VK_TRUE, 2'000'000ull);
    if (waited == VK_TIMEOUT) {
        sc.gpuBusy = 1;
        return false;
    }
    sc.gpuBusy = 0;
    if (waited != VK_SUCCESS) {
        spdlog::error("vkWaitForFences {}", static_cast<int>(waited));
        if (waited == VK_ERROR_DEVICE_LOST) {
            deviceMarkLost(d, "vkWaitForFences", static_cast<int32_t>(waited));
        }
        return false;
    }
    vkResetFences(d.device, 1, &sc.flightFence[f]);

    uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(d.device, sc.handle, UINT64_MAX, sc.acquireSem[f], VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        spdlog::error("vkAcquireNextImageKHR {}", static_cast<int>(acq));
        if (acq == VK_ERROR_DEVICE_LOST || acq == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            deviceMarkLost(d, "vkAcquireNextImageKHR", static_cast<int32_t>(acq));
        }
        return false;
    }

    vkResetCommandPool(d.device, sc.pool[f], 0);
    VkCommandBufferBeginInfo bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(sc.cmd[f], &bi);

    fc.cmd = sc.cmd[f];
    fc.uiCmd = VK_NULL_HANDLE;
    fc.imageIndex = imageIndex;
    fc.flight = f;
    const VkImageLayout oldLayout = sc.presented[imageIndex] != 0
        ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
        : VK_IMAGE_LAYOUT_UNDEFINED;
    rhiImageBarrier(fc.cmd, sc.images[imageIndex],
        oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_ACCESS_2_NONE : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    sc.inColor[imageIndex] = 1;
    return true;
}

bool swapchainBeginUi(Swapchain& sc, FrameContext& fc) {
    if (fc.flight >= kFramesInFlight || sc.uiCmd[fc.flight] == VK_NULL_HANDLE) {
        return false;
    }
    fc.uiCmd = sc.uiCmd[fc.flight];
    const VkCommandBufferBeginInfo bi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    return vkBeginCommandBuffer(fc.uiCmd, &bi) == VK_SUCCESS;
}

void swapchainToPresent(Swapchain& sc, const FrameContext& fc) {
    const VkCommandBuffer cmd = fc.uiCmd != VK_NULL_HANDLE ? fc.uiCmd : fc.cmd;
    rhiImageBarrier(cmd, sc.images[fc.imageIndex],
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    if (fc.imageIndex < 8) {
        sc.presented[fc.imageIndex] = 1;
        sc.inColor[fc.imageIndex] = 0;
    }
}

bool swapchainSubmitPresent(Swapchain& sc, Device& d, FrameContext& fc, bool pace) {
    vkEndCommandBuffer(fc.cmd);
    const bool split = fc.uiCmd != VK_NULL_HANDLE;
    if (split) {
        vkEndCommandBuffer(fc.uiCmd);
    }

    VkSemaphoreSubmitInfo waitSem[2]{};
    waitSem[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSem[0].semaphore = sc.acquireSem[fc.flight];
    waitSem[0].stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    uint32_t waitCount = 1;
    if (sc.orderSem != VK_NULL_HANDLE && sc.orderValue > 0) {
        waitSem[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSem[1].semaphore = sc.orderSem;
        waitSem[1].value = sc.orderValue;
        waitSem[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitCount = 2;
    }
    VkSemaphoreSubmitInfo sigSem[2]{};
    sigSem[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sigSem[0].semaphore = sc.renderSem[fc.imageIndex];
    sigSem[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    uint32_t sigCount = 1;
    if (sc.orderSem != VK_NULL_HANDLE) {
        sigSem[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        sigSem[1].semaphore = sc.orderSem;
        sigSem[1].value = sc.orderValue + 1;
        sigSem[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        sigCount = 2;
    }
    VkCommandBufferSubmitInfo cbs[2]{};
    cbs[0].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbs[0].commandBuffer = fc.cmd;
    cbs[1].sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbs[1].commandBuffer = fc.uiCmd;
    VkSemaphoreSubmitInfo uiWait{};
    uiWait.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    uiWait.semaphore = sc.uiSem[fc.flight];
    uiWait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo sceneSig{};
    sceneSig.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    sceneSig.semaphore = sc.uiSem[fc.flight];
    sceneSig.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo2 sceneSi{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = waitCount,
        .pWaitSemaphoreInfos = waitSem,
        .commandBufferInfoCount = split ? 1u : 1u,
        .pCommandBufferInfos = cbs,
        .signalSemaphoreInfoCount = split ? 1u : sigCount,
        .pSignalSemaphoreInfos = split ? &sceneSig : sigSem,
    };
    VkSubmitInfo2 uiSi{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &uiWait,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbs[1],
        .signalSemaphoreInfoCount = sigCount,
        .pSignalSemaphoreInfos = sigSem,
    };
    // Очередь общая на все окна — несколько окон могут писать кадр параллельно (job system,
    // core/host/Host.cpp), submit/present на одной VkQueue из разных потоков без внешней
    // синхронизации запрещён спецификацией.
    std::unique_lock<std::mutex> queueLock(d.queueMutex);
    if (deviceLost(d)) {
        spdlog::error("RHI submit skipped, device lost");
        return false;
    }
    VkResult sub = vkQueueSubmit2(d.graphicsQueue, 1, &sceneSi, split ? VK_NULL_HANDLE : sc.flightFence[fc.flight]);
    if (sub == VK_SUCCESS && split) {
        sub = vkQueueSubmit2(d.graphicsQueue, 1, &uiSi, sc.flightFence[fc.flight]);
    }
    if (sub == VK_SUCCESS && sc.orderSem != VK_NULL_HANDLE) {
        sc.orderValue += 1;
    }
    if (sub != VK_SUCCESS) {
        spdlog::error("vkQueueSubmit2 failed: {}", static_cast<int>(sub));
        if (sub == VK_ERROR_DEVICE_LOST || sub == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            deviceMarkLost(d, "vkQueueSubmit2", static_cast<int32_t>(sub));
        }
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
    queueLock.unlock(); // остальное (present-wait, пэйсинг) не трогает очередь — не держим других окон.
    if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
        return false;
    }
    if (pres != VK_SUCCESS) {
        spdlog::error("vkQueuePresentKHR {}", static_cast<int>(pres));
        if (pres == VK_ERROR_DEVICE_LOST || pres == VK_ERROR_OUT_OF_DEVICE_MEMORY) {
            deviceMarkLost(d, "vkQueuePresentKHR", static_cast<int32_t>(pres));
        }
        return false;
    }
    if (sc.clickTick != 0 && d.caps.presentWait && d.caps.presentId) {
        const uint64_t freq = platformFrequency();
        if (vkWaitForPresentKHR(d.device, sc.handle, pid, 50'000'000ull) == VK_SUCCESS && freq != 0) {
            const uint64_t now = platformCounter();
            sc.photonMs = static_cast<float>(now - sc.clickTick) * 1000.0f / static_cast<float>(freq);
        }
        sc.clickTick = 0;
    }

    if (pace && sc.asked.present != Present::Immediate && d.caps.presentWait && vkWaitForPresentKHR != nullptr) {
        vkWaitForPresentKHR(d.device, sc.handle, pid, UINT64_MAX);
    }

    sc.frame = (sc.frame + 1) % kFramesInFlight;
    return true;
}

} // namespace burnhope
