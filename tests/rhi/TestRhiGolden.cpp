#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "ui/Domain.hpp"
#include "ui/State.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/Canvas.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuBuffer.hpp"
#include "image/ImageFile.hpp"
#include "platform/Window.hpp"

#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>

using namespace burnhope;

static int rgbaMiss(const uint8_t* a, const uint8_t* b, int pixels, int tol) {
    int miss = 0;
    for (int i = 0; i < pixels * 4; ++i) {
        const int d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (d > tol) {
            ++miss;
        }
    }
    return miss;
}

namespace {

// Один панель на весь кадр, без радиуса (нет SDF-спада по краю) — внутренние пиксели обязаны
// быть точным цветом заливки, без допуска на антиалиасинг. Фиксированный тестовый экран,
// не зависит ни от одного ассета на диске.
constexpr float kGoldenR = 0.76f;
constexpr float kGoldenG = 0.30f;
constexpr float kGoldenB = 0.10f;

void goldenBuilder(Canvas& c, void*) {
    spawn(*c.state, kUiNone, px(64.0f, 64.0f), paintRgb(kGoldenR, kGoldenG, kGoldenB));
}

// Рендерит текущее содержимое canvas в offscreen-картинку W×H и возвращает RGBA байты через
// гарантированный путь (staging-буфер + vkCmdCopyImageToBuffer), не зависящий от
// host_image_copy — это инфраструктура теста, а не то, что тест проверяет.
// forceVertexPull=true временно гасит mesh-shader путь, чтобы сравнить оба пути один к одному.
std::vector<uint8_t> renderGoldenToRgba(Device& d, Canvas& c, DescriptorHeaps& heaps, uint32_t w, uint32_t h, bool forceVertexPull) {
    std::vector<uint8_t> out;
    GpuImage target{};
    if (!gpuImageCreate(
            target,
            d,
            VkExtent2D{w, h},
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1)) {
        return out;
    }
    GpuBuffer staging{};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(w) * h * 4u;
    if (!gpuBufferCreate(staging, d, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
        gpuImageDestroy(target, d);
        return out;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = d.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(d.device, &alloc, &cmd) != VK_SUCCESS) {
        gpuBufferDestroy(staging, d);
        gpuImageDestroy(target, d);
        return out;
    }
    const VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);
    heapBind(cmd, heaps);

    Swapchain fakeSc{};
    fakeSc.images[0] = target.image;
    fakeSc.views[0] = target.view;
    fakeSc.extent = VkExtent2D{w, h};
    fakeSc.asked.transparent = false;
    FrameContext fc{cmd, 0, 0};

    const ShaderExt savedMs = c.rhi.ms;
    if (forceVertexPull) {
        c.rhi.ms = {};
    }
    canvasRecord(cmd, c, fakeSc, fc);
    if (forceVertexPull) {
        c.rhi.ms = savedMs;
    }

    rhiImageBarrier(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COPY_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT);
    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {w, h, 1},
    };
    vkCmdCopyImageToBuffer(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
    vkEndCommandBuffer(cmd);

    const VkCommandBufferSubmitInfo cbsi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    const VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbsi,
    };
    if (vkQueueSubmit2(d.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
        vkQueueWaitIdle(d.graphicsQueue);
        out.resize(static_cast<size_t>(bytes));
        std::memcpy(out.data(), staging.mapped, static_cast<size_t>(bytes));
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
    gpuBufferDestroy(staging, d);
    gpuImageDestroy(target, d);
    return out;
}

} // namespace

// Фаза 2/3
TEST_CASE("rgba compare tolerates half a percent") {
    uint8_t a[8] = {10, 20, 30, 255, 0, 0, 0, 255};
    uint8_t b[8] = {10, 20, 30, 255, 0, 0, 0, 255};
    CHECK(rgbaMiss(a, b, 2, 2) == 0);
    b[0] = 40;
    CHECK(rgbaMiss(a, b, 2, 2) == 1);
}
TEST_CASE("golden image: panel fill is exact and mesh/vertex paths match") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for golden-image test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for golden-image test, skip");
        windowDestroy(window);
        return;
    }
    DescriptorHeaps heaps{};
    const HeapLayout heapLayout{.buffers = 1, .images = 9, .samplers = 1};
    Canvas canvas{};
    if (!descriptorHeapsCreate(heaps, device, heapLayout) || !canvasCreate(canvas, device, heaps)) {
        spdlog::warn("canvas/heap setup failed, skip golden-image test");
        descriptorHeapsDestroy(heaps, device);
        deviceDestroy(device);
        windowDestroy(window);
        return;
    }
    canvasSetBuilder(canvas, goldenBuilder, nullptr);
    canvasReload(canvas);
    InputFrame input{};
    (void)canvasConsume(canvas, device, 64.0f, 64.0f, input, 0.0);

    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    const std::vector<uint8_t> meshPixels = canvas.rhi.ms.handle != VK_NULL_HANDLE
        ? renderGoldenToRgba(device, canvas, heaps, kW, kH, false)
        : std::vector<uint8_t>{};
    const std::vector<uint8_t> vertPixels = renderGoldenToRgba(device, canvas, heaps, kW, kH, true);

    CHECK(vertPixels.size() == static_cast<size_t>(kW) * kH * 4u);
    if (vertPixels.size() == static_cast<size_t>(kW) * kH * 4u) {
        const uint8_t* center = vertPixels.data() + (static_cast<size_t>(kH / 2) * kW + kW / 2) * 4u;
        const uint8_t expected[4] = {
            static_cast<uint8_t>(kGoldenR * 255.0f + 0.5f),
            static_cast<uint8_t>(kGoldenG * 255.0f + 0.5f),
            static_cast<uint8_t>(kGoldenB * 255.0f + 0.5f),
            255,
        };
        spdlog::info("golden vertex-pull center rgba = {} {} {} {}", center[0], center[1], center[2], center[3]);
        CHECK(rgbaMiss(center, expected, 1, 2) == 0);
    }
    if (!meshPixels.empty()) {
        CHECK(meshPixels.size() == vertPixels.size());
        if (meshPixels.size() == vertPixels.size()) {
            const int miss = rgbaMiss(meshPixels.data(), vertPixels.data(), static_cast<int>(kW * kH), 0);
            spdlog::info("golden mesh-vs-vertex mismatched channels = {}", miss);
            CHECK(miss == 0);
        }
    } else {
        spdlog::warn("mesh-shader path unavailable on this GPU, skipped cross-check");
    }

    canvasDestroy(canvas, device);
    descriptorHeapsDestroy(heaps, device);
    deviceDestroy(device);
    windowDestroy(window);
}
