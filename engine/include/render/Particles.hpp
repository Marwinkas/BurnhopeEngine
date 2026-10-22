#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kParticleCap = 8192;
constexpr uint32_t kParticleSlice = 256u + kParticleCap * 64u;
constexpr uint32_t kParticleBurstCap = 32;
constexpr uint32_t kParticleFlashCap = 8;
constexpr uint32_t kParticleSlot = 80;
constexpr uint32_t kParticleSpawnSlot = 81;
constexpr uint32_t kParticleIndirectSlot = 82;

struct alignas(16) ParticlePush {
    uint32_t flight = 0;
    uint32_t mode = 0;
    float dt = 0.0f;
    uint32_t pad = 0;
};
static_assert(sizeof(ParticlePush) == 16);

struct alignas(16) ParticleBurst {
    float pos[3]{};
    float count = 0.0f;
    float seed = 0.0f;
    float pad[3]{};
};
static_assert(sizeof(ParticleBurst) == 32);

struct alignas(16) ParticleSpawn {
    uint32_t count = 0;
    uint32_t total = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    ParticleBurst burst[kParticleBurstCap]{};
};
static_assert(sizeof(ParticleSpawn) == 16u + sizeof(ParticleBurst) * kParticleBurstCap);

struct ParticleFlash {
    float pos[3]{};
    float candela = 0.0f;
    float radius = 0.0f;
    float life = 0.0f;
    float maxLife = 0.0f;
};

struct ParticlePass {
    ShaderExt sim{};
    ShaderExt mesh{};
    ShaderExt frag{};
    GpuBuffer particles{};
    GpuBuffer spawn{};
    GpuBuffer indirect{};
    ParticleBurst queue[kParticleBurstCap]{};
    uint32_t queueCount = 0;
    uint32_t queueTotal = 0;
    ParticleFlash flash[kParticleFlashCap]{};
    uint32_t flashCount = 0;
    uint32_t seed = 1;
    uint8_t intro = 0;
    uint8_t gpuLive = 0;
};

[[nodiscard]] bool particlePassCreate(ParticlePass& pass, Device& device, DescriptorHeaps& heaps);
void particlePassDestroy(ParticlePass& pass, Device& device);
void particleBurst(ParticlePass& pass, float x, float y, float z);
void particleTick(ParticlePass& pass, float dt);
void particleRecord(
    VkCommandBuffer cmd,
    Device& device,
    ParticlePass& pass,
    TrackedImage& hdr,
    VkExtent2D extent,
    uint32_t flight,
    float dt);

} // namespace burnhope
