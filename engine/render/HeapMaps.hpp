#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"

namespace burnhope {

// Visbuffer frame slots. The heap itself is untyped; these indices are this pass.
enum class HeapBuf : uint32_t {
    Frame = 0,
    Verts = 1,
    Indices = 2,
    Meshlets = 3,
    Instances = 4,
    Visible = 5,
    Materials = 6,
    ShadowVisible = 7,
    PointVisible = 10,
    CubeVisible = 13,
    Decals = 8,
    Probes = 9,
    Sectors = 30,
    ShadowPages = 27,
    TexPages = 28,
    TexFeedback = 29,
    Lights = 19,
    Clusters = 20,
    ClusterIndex = 21,
    // 48 — буфер примитивов холста (kUiDescPrims). Сюда писать нельзя.
    Rc = 18,
    Post = 31,
    Flags = 32,
    Gtao = 33,
    Ssr = 34,
    Ssrc = 35,
    RcParams = 36,
    Shade = 37,
    Sliders = 38,
    Bloom = 39,
    Candidates = 23,
    Indirect = 24,
    ShadowAll = 25,
    ShadowIndirect = 26,
    Count = 40,
};

// Буферы и картинки — разные номера. 0..15 геометрия кадра. 16..31 проходы (22 занят холстом).
// 32..47 блоки ползунков. Картинка при ресайзе остаётся на своём HeapImg.
static_assert(static_cast<uint32_t>(HeapBuf::Frame) < 16u);
static_assert(static_cast<uint32_t>(HeapBuf::Rc) >= 16u && static_cast<uint32_t>(HeapBuf::Rc) < 32u);
static_assert(static_cast<uint32_t>(HeapBuf::Rc) != 48u);
static_assert(static_cast<uint32_t>(HeapBuf::Flags) >= 32u && static_cast<uint32_t>(HeapBuf::Bloom) < 48u);

enum class HeapImg : uint32_t {
    Vis = 0,
    Depth = 1,
    HdrAStorage = 2,
    HdrASampled = 3,
    HdrBStorage = 4,
    HdrBSampled = 5,
    Shadow = 6,
    ShadowB = 7,
    Cascade = 8,
    Textures = 16,
    PointShadow = 784,
    Cube = 785,
    HiZ = 786,
    HiZSample = 787,
    HiZ1 = 788,
    HiZSample1 = 789,
    HiZMax = 790,
    HiZMaxSample = 791,
    HiZMax1 = 792,
    HiZMaxSample1 = 793,
    AoStorage = 794,
    Ao = 795,
    SsrHalf = 796,
    BloomStorage = 797,
    Bloom = 798,
    Count = 661,
};

enum class HeapSamp : uint32_t {
    Nearest = 0,
    Linear = 1,
    Wrap = 2,
    Count = 3,
};

// Буфер кучи: binding шейдера и имя слота. Смещение считает куча.
inline VkDescriptorSetAndBindingMappingEXT heapMapBuf(
    const DescriptorHeaps& h, uint32_t binding, VkSpirvResourceTypeFlagsEXT kind, HeapBuf slot) {
    return heapMap(0, binding, kind, heapBufOffset(h, static_cast<uint32_t>(slot)), static_cast<uint32_t>(h.bufferDescSize));
}

// Storage-буфер в именованный слот. Нулевой адрес не пишется.
[[nodiscard]] inline bool heapWriteStorage(DescriptorHeaps& h, Device& d, uint32_t slot, VkDeviceAddress address, VkDeviceSize size) {
    if (address == 0 || size == 0) {
        return false;
    }
    return heapWriteBuffer(h, d, slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, address, size);
}

[[nodiscard]] inline bool heapWriteStorage(DescriptorHeaps& h, Device& d, HeapBuf slot, const GpuBuffer& buf) {
    return heapWriteStorage(h, d, static_cast<uint32_t>(slot), buf.address, buf.size);
}

// Картинка кучи. Ресайз пишет тот же HeapImg, не новый номер.
inline VkDescriptorSetAndBindingMappingEXT heapMapImg(
    const DescriptorHeaps& h, uint32_t binding, VkSpirvResourceTypeFlagsEXT kind, HeapImg slot) {
    return heapMap(0, binding, kind, heapImgOffset(h, static_cast<uint32_t>(slot)), static_cast<uint32_t>(h.imageDescSize));
}

inline VkDescriptorSetAndBindingMappingEXT heapMapBlock(const DescriptorHeaps& h, uint32_t binding, HeapBuf slot) {
    return heapMap(0, binding, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(slot)),
        static_cast<uint32_t>(h.bufferDescSize));
}

inline void heapMapsVis(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[2] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
    out[3] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[4] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[5] = heapMap(0, 11, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Visible)), buf);
}

inline void heapMapsVisFrag(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[9]) {
    heapMapsVis(h, out);
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    out[6] = heapMap(0, 12, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Textures)), img);
    out[7] = heapMap(0, 14, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Materials)), buf);
    out[8] = heapMap(0, 24, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Wrap)), samp);
}

inline void heapMapsShadow(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[7], uint32_t cascade) {
    heapMapsVis(h, out);
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    out[6] = heapMap(0, 18, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ShadowVisible) + cascade), buf);
}

inline void heapMapsPoint(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[7], uint32_t light) {
    heapMapsVis(h, out);
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    out[6] = heapMap(0, 19, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::PointVisible) + light), buf);
}

inline void heapMapsCube(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[9], uint32_t face) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[2] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
    out[3] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[4] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[5] = heapMap(0, 12, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Textures)), img);
    out[6] = heapMap(0, 13, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), samp);
    out[7] = heapMap(0, 14, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Materials)), buf);
    out[8] = heapMap(0, 21, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::CubeVisible) + face), buf);
}

inline void heapMapsShade(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[26]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Vis)), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[3] = heapMap(0, 3, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrAStorage)), img);
    out[4] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[5] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
    out[6] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[7] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[8] = heapMap(0, 12, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Textures)), img);
    out[9] = heapMap(0, 13, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), samp);
    out[10] = heapMap(0, 14, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Materials)), buf);
    out[11] = heapMap(0, 15, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Shadow)), img);
    out[12] = heapMap(0, 16, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::ShadowB)), img);
    out[13] = heapMap(0, 17, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::PointShadow)), img);
    out[14] = heapMap(0, 24, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Wrap)), samp);
    out[15] = heapMap(0, 25, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Lights)), buf);
    out[16] = heapMap(0, 26, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Clusters)), buf);
    out[17] = heapMap(0, 27, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ClusterIndex)), buf);
    out[18] = heapMap(0, 18, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Decals)), buf);
    out[19] = heapMap(0, 19, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Probes)), buf);
    out[20] = heapMap(0, 28, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::TexPages)), buf);
    out[21] = heapMap(0, 29, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::TexFeedback)), buf);
    out[22] = heapMap(0, 31, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ShadowPages)), buf);
    out[23] = heapMap(0, 32, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Ao)), img);
    out[24] = heapMapBlock(h, 50, HeapBuf::Flags);
    out[25] = heapMapBlock(h, 55, HeapBuf::Shade);
}

inline void heapMapsGtao(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[5]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[2] = heapMap(0, 32, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::AoStorage)), img);
    out[3] = heapMapBlock(h, 50, HeapBuf::Flags);
    out[4] = heapMapBlock(h, 51, HeapBuf::Gtao);
}

inline void heapMapsCluster(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[5]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 25, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Lights)), buf);
    out[2] = heapMap(0, 26, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Clusters)), buf);
    out[3] = heapMap(0, 27, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ClusterIndex)), buf);
    out[4] = heapMap(0, 18, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Decals)), buf);
}

inline void heapMapsShadowCull(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[4]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[2] = heapMap(0, 18, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ShadowAll)), buf);
    out[3] = heapMap(0, 32, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::ShadowIndirect)), buf);
}

inline void heapMapsHizMax(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[3], uint32_t flight) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t slot = flight == 0u ? static_cast<uint32_t>(HeapImg::HiZMax) : static_cast<uint32_t>(HeapImg::HiZMax1);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[2] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, slot), img);
}

inline void heapMapsHiz(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[3], uint32_t flight) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t slot = flight == 0u ? static_cast<uint32_t>(HeapImg::HiZ) : static_cast<uint32_t>(HeapImg::HiZ1);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[2] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, slot), img);
}

inline void heapMapsSsr(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[13], uint32_t flight) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    const uint32_t hiz = flight == 0u ? static_cast<uint32_t>(HeapImg::HiZMaxSample) : static_cast<uint32_t>(HeapImg::HiZMaxSample1);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Vis)), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[3] = heapMap(0, 4, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrASampled)), img);
    out[4] = heapMap(0, 5, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBStorage)), img);
    out[5] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), samp);
    out[6] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, hiz), img);
    out[7] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Cube)), img);
    out[8] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
    out[9] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[10] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[11] = heapMap(0, 11, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[12] = heapMap(0, 12, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::SsrHalf)), img);
}

inline void heapMapsOccl(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[10]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HiZSample)), img);
    out[2] = heapMap(0, 10, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Instances)), buf);
    out[3] = heapMap(0, 11, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Visible)), buf);
    out[4] = heapMap(0, 28, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Candidates)), buf);
    out[5] = heapMap(0, 29, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indirect)), buf);
    out[6] = heapMap(0, 31, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HiZSample1)), img);
    out[7] = heapMap(0, 9, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Meshlets)), buf);
    out[8] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Sectors)), buf);
    out[9] = heapMapBlock(h, 50, HeapBuf::Flags);
}

inline void heapMapsCmaa(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[4]) {
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), static_cast<uint32_t>(h.bufferDescSize));
    out[1] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBSampled)), static_cast<uint32_t>(h.imageDescSize));
    out[2] = heapMap(0, 3, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrAStorage)), static_cast<uint32_t>(h.imageDescSize));
    out[3] = heapMapBlock(h, 50, HeapBuf::Flags);
}

inline void heapMapsTonemap(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    out[0] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrASampled)), static_cast<uint32_t>(h.imageDescSize));
    out[1] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), static_cast<uint32_t>(h.samplerDescSize));
    out[2] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), static_cast<uint32_t>(h.bufferDescSize));
    out[3] = heapMap(0, 33, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Bloom)), static_cast<uint32_t>(h.imageDescSize));
    out[4] = heapMapBlock(h, 50, HeapBuf::Flags);
    out[5] = heapMapBlock(h, 57, HeapBuf::Bloom);
}

inline void heapMapsBloom(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[4]) {
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), static_cast<uint32_t>(h.bufferDescSize));
    out[1] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBSampled)), static_cast<uint32_t>(h.imageDescSize));
    out[2] = heapMap(0, 33, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::BloomStorage)), static_cast<uint32_t>(h.imageDescSize));
    out[3] = heapMapBlock(h, 57, HeapBuf::Bloom);
}

} // namespace burnhope
