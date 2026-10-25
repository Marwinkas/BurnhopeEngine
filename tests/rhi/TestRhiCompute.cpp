#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "rhi/Device.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuBuffer.hpp"
#include "image/ImageFile.hpp"
#include "platform/Window.hpp"

#include <cmath>
#include <cstring>
#include <vector>
#include <spdlog/spdlog.h>

using namespace burnhope;

namespace {

void decodeBc1Block(const uint8_t* block, uint8_t out[16 * 4]) {
    const uint16_t c0 = static_cast<uint16_t>(block[0] | (block[1] << 8));
    const uint16_t c1 = static_cast<uint16_t>(block[2] | (block[3] << 8));
    const uint32_t idx = static_cast<uint32_t>(block[4]) | (static_cast<uint32_t>(block[5]) << 8)
        | (static_cast<uint32_t>(block[6]) << 16) | (static_cast<uint32_t>(block[7]) << 24);
    auto unpack565 = [](uint16_t v, uint8_t rgb[3]) {
        rgb[0] = static_cast<uint8_t>(((v >> 11) & 0x1fu) * 255u / 31u);
        rgb[1] = static_cast<uint8_t>(((v >> 5) & 0x3fu) * 255u / 63u);
        rgb[2] = static_cast<uint8_t>((v & 0x1fu) * 255u / 31u);
    };
    uint8_t col[4][3];
    unpack565(c0, col[0]);
    unpack565(c1, col[1]);
    // Шейдер (core/image/compress.slang) гарантирует c0 > c1 как uint16 — всегда 4-цветный
    // непрозрачный режим BC1, никогда 3-цветный с "прозрачным чёрным".
    for (int ch = 0; ch < 3; ++ch) {
        col[2][ch] = static_cast<uint8_t>((2u * col[0][ch] + col[1][ch]) / 3u);
        col[3][ch] = static_cast<uint8_t>((col[0][ch] + 2u * col[1][ch]) / 3u);
    }
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t sel = (idx >> (i * 2u)) & 3u;
        out[i * 4 + 0] = col[sel][0];
        out[i * 4 + 1] = col[sel][1];
        out[i * 4 + 2] = col[sel][2];
        out[i * 4 + 3] = 255;
    }
}

uint32_t bc7Bits(const uint8_t* block, uint32_t& pos, uint32_t n) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t byte = pos >> 3;
        const uint32_t bit = pos & 7u;
        v |= ((block[byte] >> bit) & 1u) << i;
        ++pos;
    }
    return v;
}

bool decodeBc7Mode6(const uint8_t* block, uint8_t out[16 * 4]) {
    uint32_t pos = 0;
    uint32_t zeros = 0;
    while (pos < 8 && ((block[0] >> pos) & 1u) == 0) {
        ++zeros;
        ++pos;
    }
    if (zeros != 6 || pos >= 128) {
        return false;
    }
    ++pos;
    int ep[2][4];
    for (int c = 0; c < 4; ++c) {
        ep[0][c] = static_cast<int>(bc7Bits(block, pos, 7));
        ep[1][c] = static_cast<int>(bc7Bits(block, pos, 7));
    }
    const int p0 = static_cast<int>(bc7Bits(block, pos, 1));
    const int p1 = static_cast<int>(bc7Bits(block, pos, 1));
    for (int c = 0; c < 4; ++c) {
        ep[0][c] = (ep[0][c] << 1) | p0;
        ep[1][c] = (ep[1][c] << 1) | p1;
    }
    static constexpr int kW[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t n = i == 0 ? 3u : 4u;
        const uint32_t sel = bc7Bits(block, pos, n);
        if (sel > 15) {
            return false;
        }
        const int w = kW[sel];
        for (int c = 0; c < 4; ++c) {
            const int rec = ((64 - w) * ep[0][c] + w * ep[1][c] + 32) >> 6;
            out[i * 4 + c] = static_cast<uint8_t>(rec < 0 ? 0 : (rec > 255 ? 255 : rec));
        }
    }
    return pos == 128;
}

float halfToFloat(uint16_t h) {
    const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exp = (h >> 10) & 0x1fu;
    const uint32_t mant = h & 0x3ffu;
    uint32_t bits = sign;
    if (exp == 31u) {
        bits |= 0x7f800000u | (mant << 13);
    } else if (exp != 0u) {
        bits |= ((exp + 127u - 15u) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

int unquant10(uint32_t q) {
    if (q == 0u) {
        return 0;
    }
    if (q >= 1023u) {
        return 0xffff;
    }
    return static_cast<int>(((q << 16) + 0x8000u) >> 10);
}

bool decodeBc6hMode11(const uint8_t* block, float out[16 * 3]) {
    uint32_t pos = 0;
    if (bc7Bits(block, pos, 5) != 0x03u) {
        return false;
    }
    int ep[2][3];
    for (int c = 0; c < 3; ++c) {
        ep[0][c] = unquant10(bc7Bits(block, pos, 10));
    }
    for (int c = 0; c < 3; ++c) {
        ep[1][c] = unquant10(bc7Bits(block, pos, 10));
    }
    static constexpr int kW[16] = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t sel = bc7Bits(block, pos, i == 0 ? 3u : 4u);
        if (sel > 15) {
            return false;
        }
        const int w = kW[sel];
        for (int c = 0; c < 3; ++c) {
            const int interp = ((64 - w) * ep[0][c] + w * ep[1][c] + 32) >> 6;
            const int half = (interp * 31) >> 6;
            out[i * 3 + c] = halfToFloat(static_cast<uint16_t>(half < 0 ? 0 : half));
        }
    }
    return pos == 128;
}

void decodeBc4(const uint8_t* block, uint8_t out[16]) {
    const int e0 = block[0];
    const int e1 = block[1];
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<uint64_t>(block[2 + i]) << (8 * i);
    }
    int pal[8];
    pal[0] = e0;
    pal[1] = e1;
    for (int k = 0; k < 6; ++k) {
        pal[k + 2] = ((6 - k) * e0 + (k + 1) * e1) / 7;
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = static_cast<uint8_t>(pal[(bits >> (i * 3)) & 7u]);
    }
}

} // namespace

// BC3/BC4/BC5
TEST_CASE("gpuCompressLevel: BC3 BC4 BC5 decode close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for BC3/4/5 test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for BC3/4/5 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 32;
    constexpr uint32_t kH = 32;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 8);
            p[1] = static_cast<uint8_t>(y * 8);
            p[2] = static_cast<uint8_t>((x + y) * 4);
            p[3] = static_cast<uint8_t>(40 + (x * 3 + y) % 180);
        }
    }
    const uint32_t blocks = (kW / 4u) * (kH / 4u);
    std::vector<uint8_t> bc4(static_cast<size_t>(blocks) * 8u);
    std::vector<uint8_t> bc5(static_cast<size_t>(blocks) * 16u);
    std::vector<uint8_t> bc3(static_cast<size_t>(blocks) * 16u);
    CHECK(gpuCompressLevel(device, ImageBlock::Bc4, rgba.data(), kW, kH, bc4.data()));
    CHECK(gpuCompressLevel(device, ImageBlock::Bc5, rgba.data(), kW, kH, bc5.data()));
    CHECK(gpuCompressLevel(device, ImageBlock::Bc3, rgba.data(), kW, kH, bc3.data()));
    CHECK(imageBlockBytes(ImageBlock::Bc4) == 8u);
    CHECK(imageBlockBytes(ImageBlock::Bc5) == 16u);
    CHECK(imageBlockBytes(ImageBlock::Bc3) == 16u);
    double sum4 = 0.0;
    double sum5 = 0.0;
    double sum3 = 0.0;
    for (uint32_t by = 0; by < kH / 4u; ++by) {
        for (uint32_t bx = 0; bx < kW / 4u; ++bx) {
            const size_t bi = static_cast<size_t>(by) * (kW / 4u) + bx;
            uint8_t rough[16];
            uint8_t r[16];
            uint8_t g[16];
            uint8_t a[16];
            uint8_t color[16 * 4];
            decodeBc4(bc4.data() + bi * 8u, rough);
            decodeBc4(bc5.data() + bi * 16u, r);
            decodeBc4(bc5.data() + bi * 16u + 8u, g);
            decodeBc1Block(bc3.data() + bi * 16u, color);
            decodeBc4(bc3.data() + bi * 16u + 8u, a);
            CHECK(bc4[bi * 8u] > bc4[bi * 8u + 1u]);
            for (uint32_t ty = 0; ty < 4; ++ty) {
                for (uint32_t tx = 0; tx < 4; ++tx) {
                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                    const uint32_t i = ty * 4u + tx;
                    sum4 += std::abs(static_cast<int>(src[0]) - static_cast<int>(rough[i]));
                    sum5 += std::abs(static_cast<int>(src[0]) - static_cast<int>(r[i]));
                    sum5 += std::abs(static_cast<int>(src[1]) - static_cast<int>(g[i]));
                    const uint8_t* dec = color + i * 4u;
                    sum3 += std::abs(static_cast<int>(src[0]) - static_cast<int>(dec[0]));
                    sum3 += std::abs(static_cast<int>(src[1]) - static_cast<int>(dec[1]));
                    sum3 += std::abs(static_cast<int>(src[2]) - static_cast<int>(dec[2]));
                    sum3 += std::abs(static_cast<int>(src[3]) - static_cast<int>(a[i]));
                }
            }
        }
    }
    const double n = static_cast<double>(kW) * kH;
    spdlog::info("gpu bc4/bc5/bc3 mean abs = {:.3f} {:.3f} {:.3f}", sum4 / n, sum5 / (n * 2.0), sum3 / (n * 4.0));
    CHECK(sum4 / n < 8.0);
    CHECK(sum5 / (n * 2.0) < 8.0);
    CHECK(sum3 / (n * 4.0) < 12.0);
    deviceDestroy(device);
    windowDestroy(window);
}
TEST_CASE("gpuImageUploadRgba: GPU BC1 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC1 test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for GPU BC1 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 4);
            p[1] = static_cast<uint8_t>(y * 4);
            p[2] = static_cast<uint8_t>((x + y) * 2);
            p[3] = 255; // без альфы -> BC1, не BC7
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadRgba(img, device, rgba.data(), kW, kH, 1);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
        const uint32_t blocksX = kW / 4u;
        const uint32_t blocksY = kH / 4u;
        const VkDeviceSize bcBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 8u;
        GpuBuffer staging{};
        if (gpuBufferCreate(staging, device, bcBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) == VK_SUCCESS) {
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(cmd, &begin);
                rhiImageBarrier(
                    cmd,
                    img.image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageExtent = {kW, kH, 1},
                };
                vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
                vkEndCommandBuffer(cmd);
                const VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
                const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi};
                if (vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
                    vkQueueWaitIdle(device.graphicsQueue);
                    const auto* blocks = static_cast<const uint8_t*>(staging.mapped);
                    double sumErr = 0.0;
                    for (uint32_t by = 0; by < blocksY; ++by) {
                        for (uint32_t bx = 0; bx < blocksX; ++bx) {
                            uint8_t decoded[16 * 4];
                            decodeBc1Block(blocks + (static_cast<size_t>(by) * blocksX + bx) * 8u, decoded);
                            for (uint32_t ty = 0; ty < 4; ++ty) {
                                for (uint32_t tx = 0; tx < 4; ++tx) {
                                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                                    const uint8_t* dec = decoded + (ty * 4 + tx) * 4u;
                                    sumErr += std::abs(static_cast<int>(src[0]) - dec[0]);
                                    sumErr += std::abs(static_cast<int>(src[1]) - dec[1]);
                                    sumErr += std::abs(static_cast<int>(src[2]) - dec[2]);
                                }
                            }
                        }
                    }
                    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 3.0);
                    spdlog::info("gpu bc1 mean abs error per channel = {:.3f}", meanErr);
                    CHECK(meanErr < 20.0);
                }
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
            }
            gpuBufferDestroy(staging, device);
        }
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}
TEST_CASE("gpuCompressLevel: GPU BC7 mode 6 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC7 test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for GPU BC7 test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x * 4);
            p[1] = static_cast<uint8_t>(y * 4);
            p[2] = static_cast<uint8_t>((x + y) * 2);
            p[3] = static_cast<uint8_t>(32 + (x + y) % 200);
        }
    }
    const uint32_t blocksX = kW / 4u;
    const uint32_t blocksY = kH / 4u;
    std::vector<uint8_t> blocks(static_cast<size_t>(blocksX) * blocksY * imageBlockBytes(ImageBlock::Bc7));
    CHECK(gpuCompressLevel(device, ImageBlock::Bc7, rgba.data(), kW, kH, blocks.data()));
    double sumErr = 0.0;
    uint32_t mode6 = 0;
    for (uint32_t by = 0; by < blocksY; ++by) {
        for (uint32_t bx = 0; bx < blocksX; ++bx) {
            uint8_t decoded[16 * 4];
            const uint8_t* blk = blocks.data() + (static_cast<size_t>(by) * blocksX + bx) * 16u;
            if (!decodeBc7Mode6(blk, decoded)) {
                continue;
            }
            ++mode6;
            for (uint32_t ty = 0; ty < 4; ++ty) {
                for (uint32_t tx = 0; tx < 4; ++tx) {
                    const uint8_t* src = rgba.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 4u;
                    const uint8_t* dec = decoded + (ty * 4 + tx) * 4u;
                    for (int c = 0; c < 4; ++c) {
                        sumErr += std::abs(static_cast<int>(src[c]) - static_cast<int>(dec[c]));
                    }
                }
            }
        }
    }
    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 4.0);
    spdlog::info("gpu bc7 mode6 blocks {}/{} mean abs error per channel = {:.3f}", mode6, blocksX * blocksY, meanErr);
    CHECK(mode6 == blocksX * blocksY);
    CHECK(meanErr < 12.0);
    deviceDestroy(device);
    windowDestroy(window);
}
TEST_CASE("gpuImageUploadHdr: GPU BC6H mode 11 decodes close to the source") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for GPU BC6H test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for GPU BC6H test, skip");
        windowDestroy(window);
        return;
    }
    constexpr uint32_t kW = 64;
    constexpr uint32_t kH = 64;
    std::vector<float> rgb(static_cast<size_t>(kW) * kH * 3u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            float* p = rgb.data() + (static_cast<size_t>(y) * kW + x) * 3u;
            p[0] = static_cast<float>(x) / 63.0f * 4.0f;
            p[1] = static_cast<float>(y) / 63.0f * 2.0f;
            p[2] = static_cast<float>(x + y) / 126.0f * 8.0f;
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadHdr(img, device, rgb.data(), kW, kH);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.format == VK_FORMAT_BC6H_UFLOAT_BLOCK);
        const uint32_t blocksX = kW / 4u;
        const uint32_t blocksY = kH / 4u;
        const VkDeviceSize bcBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 16u;
        GpuBuffer staging{};
        if (gpuBufferCreate(staging, device, bcBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
            VkCommandBuffer cmd = VK_NULL_HANDLE;
            const VkCommandBufferAllocateInfo alloc{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = device.commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) == VK_SUCCESS) {
                const VkCommandBufferBeginInfo begin{
                    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                };
                vkBeginCommandBuffer(cmd, &begin);
                rhiImageBarrier(
                    cmd,
                    img.image,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                    VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
                const VkBufferImageCopy region{
                    .bufferOffset = 0,
                    .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                    .imageExtent = {kW, kH, 1},
                };
                vkCmdCopyImageToBuffer(cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &region);
                vkEndCommandBuffer(cmd);
                const VkCommandBufferSubmitInfo cbsi{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd};
                const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbsi};
                if (vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS) {
                    vkQueueWaitIdle(device.graphicsQueue);
                    const auto* blocks = static_cast<const uint8_t*>(staging.mapped);
                    double sumErr = 0.0;
                    uint32_t mode11 = 0;
                    for (uint32_t by = 0; by < blocksY; ++by) {
                        for (uint32_t bx = 0; bx < blocksX; ++bx) {
                            float decoded[16 * 3];
                            const uint8_t* blk = blocks + (static_cast<size_t>(by) * blocksX + bx) * 16u;
                            if (!decodeBc6hMode11(blk, decoded)) {
                                continue;
                            }
                            ++mode11;
                            for (uint32_t ty = 0; ty < 4; ++ty) {
                                for (uint32_t tx = 0; tx < 4; ++tx) {
                                    const float* src = rgb.data() + (static_cast<size_t>(by * 4 + ty) * kW + (bx * 4 + tx)) * 3u;
                                    const float* dec = decoded + (ty * 4 + tx) * 3u;
                                    for (int c = 0; c < 3; ++c) {
                                        sumErr += std::abs(src[c] - dec[c]);
                                    }
                                }
                            }
                        }
                    }
                    const double meanErr = sumErr / (static_cast<double>(kW) * kH * 3.0);
                    spdlog::info("gpu bc6h mode11 blocks {}/{} mean abs error = {:.5f}", mode11, blocksX * blocksY, meanErr);
                    CHECK(mode11 == blocksX * blocksY);
                    CHECK(meanErr < 0.05);
                }
            }
            if (cmd != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
            }
            gpuBufferDestroy(staging, device);
        }
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}
TEST_CASE("gpuImageUploadRgba round-trips through host_image_copy") {
    Window window{};
    WindowConfig cfg{};
    cfg.hidden = true;
    cfg.width = 64;
    cfg.height = 64;
    if (!windowCreate(window, cfg)) {
        spdlog::warn("no display for host_image_copy test, skip");
        return;
    }
    Device device{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!deviceCreateInstance(device) || !windowCreateSurface(window, device.instance, surface) || !deviceCreate(device, surface)) {
        spdlog::warn("no Vulkan device for host_image_copy test, skip");
        windowDestroy(window);
        return;
    }
    spdlog::info("host_image_copy test: caps.hostImageCopy={}", device.caps.hostImageCopy);

    constexpr uint32_t kW = 256;
    constexpr uint32_t kH = 256;
    std::vector<uint8_t> rgba(static_cast<size_t>(kW) * kH * 4u);
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            uint8_t* p = rgba.data() + (static_cast<size_t>(y) * kW + x) * 4u;
            p[0] = static_cast<uint8_t>(x);
            p[1] = static_cast<uint8_t>(y);
            p[2] = static_cast<uint8_t>(x ^ y);
            p[3] = static_cast<uint8_t>((x + y) % 251); // alpha varies -> forces BC7, not BC1
        }
    }
    GpuImage img{};
    const bool uploaded = gpuImageUploadRgba(img, device, rgba.data(), kW, kH, 1);
    CHECK(uploaded);
    if (uploaded) {
        CHECK(img.extent.width == kW);
        CHECK(img.extent.height == kH);
        CHECK(img.image != VK_NULL_HANDLE);
        CHECK(img.view != VK_NULL_HANDLE);
        gpuImageDestroy(img, device);
    }
    deviceDestroy(device);
    windowDestroy(window);
}
