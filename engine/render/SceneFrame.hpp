#pragma once

#include "gfx/Decal.hpp"
#include "gfx/Portal.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Swapchain.hpp"
#include "scene/FlyCam.hpp"

namespace burnhope {

struct SceneFrame;

[[nodiscard]] SceneFrame* sceneFrameCreate();
[[nodiscard]] bool sceneFrameLoadBhop(SceneFrame* scene, const char* path);
void sceneFrameDestroy(SceneFrame* scene, Device& device);
void sceneFrameReleaseGpu(SceneFrame* scene, Device& device);
[[nodiscard]] bool sceneFrameTick(SceneFrame* scene, const InputFrame& input, float dt);
[[nodiscard]] const char* sceneFrameTitle(const SceneFrame* scene);
void sceneFrameStats(const SceneFrame* scene, char* out, size_t cap);
void sceneFrameDebug(SceneFrame* scene, bool ssr, bool ssrc, bool hiz, bool cone, bool lod, bool frustum, bool subpixel, bool shadow, uint32_t mode);

struct SceneTweaks {
    int32_t forceLod = -1;
    // Биты как у галок по умолчанию. 128 (RC) и 64 (заморозка) выключены: иначе первый кадр,
    // ещё до чтения холста, печёт BLAS и теряет устройство.
    uint32_t passMask = 63u;
    float gtaoRadius = 2.5f;
    float gtaoSteps = 6.0f;
    float gtaoSlices = 6.0f;
    float gtaoPower = 1.6f;
    float gtaoFalloff = 1.0f;
    float ssrCutoff = 0.82f;
    float ssrSteps = 32.0f;
    float ssrStride = 0.025f;
    float ssrThick = 0.06f;
    float ssrMax = 180.0f;
    float ssrBias = 0.15f;
    float sscsReach = 30.0f;
    float sscsThick = 1.0f;
    float sscsSteps = 20.0f;
    float sscsPx = 128.0f;
    float ssrcBase = 2.0f;
    float ssrcThick = 0.03f;
    float ssrcCap = 0.6f;
    float ssrcSteps = 8.0f;
    float rcRays = 8.0f;
    float rcSpacing = 0.45f;
    float rcIntensity = 1.0f;
    float rcMax = 48.0f;
    float bloomThreshold = 0.35f;
    float fogDensity = 0.012f;
    float fogHeight = 4.0f;
    float fogScatter = 1.0f;
    uint32_t tonemapper = 0;
    bool freeze = false;
};

void sceneFrameTweaks(SceneFrame* scene, const SceneTweaks& tweaks);
[[nodiscard]] const char* sceneFrameGpuText(const SceneFrame* scene);
void sceneFrameShift(SceneFrame* scene, float dx, float dy, float dz);
[[nodiscard]] bool sceneFrameAppendBhop(SceneFrame* scene, const char* path);
// Ползунки панели. Имена, не индексы post[12].
struct HudPost {
    float grain = 0.0f;
    float bloom = 0.15f;
    float vignette = 1.1f;
    float ca = 2.0f;
    float contact = 1.0f;
    float specOcc = 1.0f;
    float toksvig = 1.0f;
    float pom = 0.35f;
    float ggx = 1.0f;
    float ssrc = 1.0f;
    float gtao = 1.2f;
    float lamp = 1.0f;
};

void sceneFrameTune(SceneFrame* scene, const HudPost& post);
void sceneFrameSetDecals(SceneFrame* scene, const DecalGpu* decals, uint32_t count);
void sceneFrameSetSectors(SceneFrame* scene, const Sector* sectors, uint32_t sectorCount, const Portal* portals, uint32_t portalCount);
// Сабмит сборки BLAS, если она уже записана. Звать после present, не из записи кадра.
void sceneFrameKick(SceneFrame* scene, Device& device);
void sceneFrameRecord(
    SceneFrame* scene,
    VkCommandBuffer cmd,
    Device& device,
    DescriptorHeaps& heaps,
    const Swapchain& swap,
    const FrameContext& frame);

} // namespace burnhope
