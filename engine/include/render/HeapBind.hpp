#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/DescriptorSlots.hpp"
#include "rhi/GpuBuffer.hpp"

namespace burnhope {

// Номера слотов кадра. Карта конкретного шейдера здесь не живёт: её собирает сам проход.
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
    RestirReservoir = 11,
    RestirNeighbor = 12,
    CubeVisible = 13,
    RestirRis = 14,
    RestirConst = 15,
    Sun = 16,
    Decals = 8,
    Probes = 9,
    Sectors = 30,
    ShadowPages = 27,
    TexPages = 28,
    TexFeedback = 29,
    Lights = 19,
    Clusters = 20,
    ClusterIndex = 21,
    Rc = 18,
    Voxel = 17,
    VxClip = 22,
    Post = 31,
    Flags = 32,
    Gtao = 33,
    Ssr = 34,
    Ssrc = 35,
    RcParams = 36,
    Shade = 37,
    Sliders = 38,
    Bloom = 39,
    Atm = 40,
    CloudNoise = 41,
    Candidates = 23,
    Indirect = 24,
    ShadowAll = 25,
    ShadowIndirect = 26,
    ShadowCascade1 = 42,
    ShadowCascade2 = 43,
    CubeFace3 = 44,
    CubeFace4 = 45,
    CubeFace5 = 46,
    Gizmo = 47,
    Count = 48,
};

// Буферы и картинки — разные номера. 0..15 геометрия. 16..31 проходы. 32..47 ползунки.
// Слот 48 — примитивы холста. Сюда писать нельзя.
static_assert(static_cast<uint32_t>(HeapBuf::Frame) < 16u);
static_assert(static_cast<uint32_t>(HeapBuf::Sun) >= 16u && static_cast<uint32_t>(HeapBuf::Sun) < 32u);
static_assert(static_cast<uint32_t>(HeapBuf::Rc) >= 16u && static_cast<uint32_t>(HeapBuf::Rc) < 32u);
static_assert(static_cast<uint32_t>(HeapBuf::Sun) != static_cast<uint32_t>(HeapBuf::Rc));
static_assert(static_cast<uint32_t>(HeapBuf::Rc) != kUiDescPrims);
static_assert(static_cast<uint32_t>(HeapBuf::Voxel) == 17u && static_cast<uint32_t>(HeapBuf::Voxel) < 32u);
static_assert(static_cast<uint32_t>(HeapBuf::VxClip) == 22u && static_cast<uint32_t>(HeapBuf::VxClip) < 32u);
static_assert(static_cast<uint32_t>(HeapBuf::Voxel) != static_cast<uint32_t>(HeapBuf::Rc));
static_assert(static_cast<uint32_t>(HeapBuf::VxClip) != static_cast<uint32_t>(HeapBuf::Sun));
static_assert(static_cast<uint32_t>(HeapBuf::CloudNoise) == 41u && static_cast<uint32_t>(HeapBuf::CloudNoise) < 48u);
static_assert(static_cast<uint32_t>(HeapBuf::Flags) >= 32u && static_cast<uint32_t>(HeapBuf::Bloom) < 48u);
static_assert(static_cast<uint32_t>(HeapBuf::Atm) == 40u && static_cast<uint32_t>(HeapBuf::Atm) < 48u);
static_assert(static_cast<uint32_t>(HeapBuf::ShadowCascade1) == 42u && static_cast<uint32_t>(HeapBuf::CubeFace5) == 46u);
static_assert(static_cast<uint32_t>(HeapBuf::ShadowCascade1) != static_cast<uint32_t>(HeapBuf::Decals));
static_assert(static_cast<uint32_t>(HeapBuf::ShadowCascade2) != static_cast<uint32_t>(HeapBuf::Probes));
static_assert(static_cast<uint32_t>(HeapBuf::CubeFace3) != static_cast<uint32_t>(HeapBuf::Sun));
static_assert(static_cast<uint32_t>(HeapBuf::CubeFace5) != static_cast<uint32_t>(HeapBuf::Rc));
static_assert(static_cast<uint32_t>(HeapBuf::RestirReservoir) == 11u);
static_assert(static_cast<uint32_t>(HeapBuf::RestirNeighbor) == 12u);
static_assert(static_cast<uint32_t>(HeapBuf::RestirRis) == 14u);
static_assert(static_cast<uint32_t>(HeapBuf::RestirConst) == 15u);
static_assert(static_cast<uint32_t>(HeapBuf::Gizmo) == 47u);
static_assert(static_cast<uint32_t>(HeapBuf::Count) <= 48u);
constexpr uint32_t kGradeSlot = 68u;
// Каскад 0 остаётся в ShadowVisible. +1 и +2 попадали в Decals и Probes.
[[nodiscard]] inline uint32_t shadowListSlot(uint32_t cascade) {
    if (cascade == 1u) {
        return static_cast<uint32_t>(HeapBuf::ShadowCascade1);
    }
    if (cascade >= 2u) {
        return static_cast<uint32_t>(HeapBuf::ShadowCascade2);
    }
    return static_cast<uint32_t>(HeapBuf::ShadowVisible);
}

// Грани 0..2 остаются в CubeVisible. +3 затирал слот солнца, +5 — слот Rc.
[[nodiscard]] inline uint32_t cubeListSlot(uint32_t face) {
    if (face == 3u) {
        return static_cast<uint32_t>(HeapBuf::CubeFace3);
    }
    if (face == 4u) {
        return static_cast<uint32_t>(HeapBuf::CubeFace4);
    }
    if (face >= 5u) {
        return static_cast<uint32_t>(HeapBuf::CubeFace5);
    }
    return static_cast<uint32_t>(HeapBuf::CubeVisible) + face;
}

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
    Cube = 800,
    CubeFill = 801,
    HiZ = 816,
    HiZSample = 817,
    HiZ1 = 818,
    HiZSample1 = 819,
    HiZMax = 820,
    HiZMaxSample = 821,
    HiZMax1 = 822,
    HiZMaxSample1 = 823,
    AoStorage = 832,
    Ao = 833,
    SsrHalf = 848,
    BloomStorage = 864,
    Bloom = 865,
    Surf0 = 880,
    Surf1 = 881,
    SkyTrans = 896,
    SkyMs = 897,
    SkyView = 898,
    Cloud = 899,
    SkyViewSample = 900,
    CloudSample = 901,
    SkyTransSample = 902,
    Contact = 903,
    ContactSample = 904,
    Reflect = 905,
    ReflectSample = 906,
    ReflectHist0 = 907,
    ReflectHist1 = 908,
    SigmaPenumbra = 909,
    SigmaSun = 910,
    SigmaHist = 911,
    // 6 выборок и 6 storage: flight * 3 + cascade. Два кадра в полёте не делят один объём.
    VoxelSample = 912,
    VoxelStore = 918,
    LightPdf = 924,
    RestirPos = 925,
    RestirMat = 926,
    SharcRawSample = 928,
    SharcHistSample = 929,
    SharcTemporal = 930,
    SharcRaw = 931,
    SharcHist = 932,
    Count = 992,
};

static_assert(static_cast<uint32_t>(HeapImg::Cascade) + 8u <= static_cast<uint32_t>(HeapImg::Textures));
static_assert(static_cast<uint32_t>(HeapImg::PointShadow) == static_cast<uint32_t>(HeapImg::Textures) + 768u);
static_assert(static_cast<uint32_t>(HeapImg::Cube) == static_cast<uint32_t>(HeapImg::PointShadow) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::HiZ) == static_cast<uint32_t>(HeapImg::Cube) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::AoStorage) == static_cast<uint32_t>(HeapImg::HiZ) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::SsrHalf) == static_cast<uint32_t>(HeapImg::AoStorage) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::BloomStorage) == static_cast<uint32_t>(HeapImg::SsrHalf) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::Surf0) == static_cast<uint32_t>(HeapImg::BloomStorage) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::SkyTrans) == static_cast<uint32_t>(HeapImg::Surf0) + 16u);
static_assert(static_cast<uint32_t>(HeapImg::Count) == static_cast<uint32_t>(HeapImg::SkyTrans) + 96u);
static_assert(static_cast<uint32_t>(HeapImg::ReflectHist0) == 907u);
static_assert(static_cast<uint32_t>(HeapImg::ReflectHist1) == 908u);
static_assert(static_cast<uint32_t>(HeapImg::ReflectHist1) < static_cast<uint32_t>(HeapImg::VoxelSample));
static_assert(static_cast<uint32_t>(HeapImg::SigmaPenumbra) == 909u);
static_assert(static_cast<uint32_t>(HeapImg::SigmaSun) == 910u);
static_assert(static_cast<uint32_t>(HeapImg::SigmaHist) == 911u);
static_assert(static_cast<uint32_t>(HeapImg::SigmaHist) < static_cast<uint32_t>(HeapImg::VoxelSample));
static_assert(static_cast<uint32_t>(HeapImg::VoxelSample) == 912u);
static_assert(static_cast<uint32_t>(HeapImg::VoxelStore) == 918u);
static_assert(static_cast<uint32_t>(HeapImg::VoxelStore) + 6u < static_cast<uint32_t>(HeapImg::Count));
static_assert(static_cast<uint32_t>(HeapImg::LightPdf) == 924u);
static_assert(static_cast<uint32_t>(HeapImg::RestirPos) == 925u);
static_assert(static_cast<uint32_t>(HeapImg::RestirMat) == 926u);
static_assert(static_cast<uint32_t>(HeapImg::RestirMat) < static_cast<uint32_t>(HeapImg::Count));
static_assert(static_cast<uint32_t>(HeapImg::SharcRawSample) == 928u);
static_assert(static_cast<uint32_t>(HeapImg::SharcHist) == 932u);
static_assert(static_cast<uint32_t>(HeapImg::SharcHist) < static_cast<uint32_t>(HeapImg::Count));

// flight 0..1, cascade 0..2. Выборка и storage — разные слоты одной картинки.
[[nodiscard]] inline uint32_t voxelImageSlot(bool store, uint32_t flight, uint32_t cascade) {
    const uint32_t base = store
        ? static_cast<uint32_t>(HeapImg::VoxelStore)
        : static_cast<uint32_t>(HeapImg::VoxelSample);
    return base + (flight & 1u) * 3u + (cascade < 3u ? cascade : 2u);
}

inline bool imageSlotLive(uint32_t slot) {
    if (slot < 16u) {
        return true;
    }
    if (slot >= 16u && slot < 784u) {
        return true;
    }
    const uint32_t base[] = {784u, 800u, 816u, 832u, 848u, 864u, 880u, 896u, 912u};
    for (uint32_t b : base) {
        if (slot >= b && slot < b + 16u) {
            return true;
        }
    }
    return slot >= 928u && slot < 992u;
}

enum class HeapSamp : uint32_t {
    Nearest = 0,
    Linear = 1,
    Wrap = 2,
    Count = 3,
};

[[nodiscard]] inline bool heapWriteStorage(DescriptorHeaps& h, Device& d, uint32_t slot, VkDeviceAddress address, VkDeviceSize size) {
    if (address == 0 || size == 0) {
        return false;
    }
    return heapWriteBuffer(h, d, slot, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, address, size);
}

[[nodiscard]] inline bool heapWriteStorage(DescriptorHeaps& h, Device& d, HeapBuf slot, const GpuBuffer& buf) {
    return heapWriteStorage(h, d, static_cast<uint32_t>(slot), buf.address, buf.size);
}

// Именованные слоты поверх сырого билдера ядра. Проход вызывает это у себя в *PassCreate.
struct PassBindings {
    HeapBindingBuilder raw;

    explicit PassBindings(const DescriptorHeaps& heaps) {
        raw.heaps = &heaps;
    }

    void sun() {
        ubo(49, HeapBuf::Sun);
    }

    void ubo(uint32_t binding, HeapBuf slot, uint32_t set = 0) {
        raw.push(set, binding, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
            heapBufOffset(*raw.heaps, static_cast<uint32_t>(slot)), static_cast<uint32_t>(raw.heaps->bufferDescSize));
    }

    void storageBuf(uint32_t binding, uint32_t slot, bool readOnly = true, uint32_t set = 0) {
        const VkSpirvResourceTypeFlagsEXT kind = readOnly
            ? VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT
            : VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT;
        raw.push(set, binding, kind, heapBufOffset(*raw.heaps, slot), static_cast<uint32_t>(raw.heaps->bufferDescSize));
    }

    void storageBuf(uint32_t binding, HeapBuf slot, bool readOnly = true, uint32_t set = 0) {
        storageBuf(binding, static_cast<uint32_t>(slot), readOnly, set);
    }

    void sampledImg(uint32_t binding, uint32_t slot, uint32_t set = 0) {
        raw.push(set, binding, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
            heapImgOffset(*raw.heaps, slot), static_cast<uint32_t>(raw.heaps->imageDescSize));
    }

    void sampledImg(uint32_t binding, HeapImg slot, uint32_t set = 0) {
        sampledImg(binding, static_cast<uint32_t>(slot), set);
    }

    void storageImg(uint32_t binding, uint32_t slot, uint32_t set = 0) {
        raw.push(set, binding, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
            heapImgOffset(*raw.heaps, slot), static_cast<uint32_t>(raw.heaps->imageDescSize));
    }

    void storageImg(uint32_t binding, HeapImg slot, uint32_t set = 0) {
        storageImg(binding, static_cast<uint32_t>(slot), set);
    }

    void sampler(uint32_t binding, HeapSamp slot, uint32_t set = 0) {
        raw.push(set, binding, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
            heapSampOffset(*raw.heaps, static_cast<uint32_t>(slot)), static_cast<uint32_t>(raw.heaps->samplerDescSize));
    }

    void knob(uint32_t binding, HeapBuf slot) {
        ubo(binding, slot);
    }
};

} // namespace burnhope
