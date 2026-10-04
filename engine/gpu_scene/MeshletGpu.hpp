#pragma once

#include "gpu_scene/Mat4.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/Device.hpp"

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct GpuVertex {
    float px, py, pz, pad0;
    float nx, ny, nz, pad1;
};

// Вид, проекция, каскады солнца. Размер заморожен: новый ползунок сюда не вставляется.
struct FrameView {
    float viewProj[16];
    float invViewProj[16];
    float sunDirIntensity[4];
    float sunColor[4];
    float cameraPos[4];
    float invExtent[2];
    float padInv[2];
    uint32_t extent[2];
    uint32_t padExt[2];
    float pointPos[3][4]{};
    float pointColor[3][4]{};
    float sunCascade[3][16]{};
    float sunSplit[4]{};
    float pointFace[3][6][16]{};
    float cubeFace[6][16]{};
    float cubeBox[8]{};
    uint32_t debugMode = 0;
    uint32_t disableSSR = 0;
    uint32_t disableSSRC = 0;
    uint32_t disableHiZ = 0;
    uint32_t disableConeCull = 0;
    uint32_t disableLod = 0;
    uint32_t disableFrustum = 0;
    uint32_t disableSubpixel = 0;
    uint32_t disableShadow = 0;
    uint32_t debugPad[3]{};
    float freezeVp[16]{};
    float viewTail[12]{};
};

// Каждый блок 64 байта и свой слот кучи. Поле внутри SSR не сдвигает GTAO и не сдвигает солнце.
struct PassFlags {
    int32_t forceLod = -1;
    uint32_t passMask = 63u;
    uint32_t pad[14]{};
};
struct GtaoParams {
    float radius = 2.5f;
    float steps = 6.0f;
    float slices = 6.0f;
    float power = 1.6f;
    float falloff = 1.0f;
    float intensity = 1.0f;
    float pad[10]{};
};
struct SsrParams {
    float cutoff = 0.82f;
    float steps = 32.0f;
    float stride = 0.025f;
    float thick = 0.06f;
    float maxDist = 180.0f;
    float bias = 0.15f;
    float pad[10]{};
};
struct SsrcParams {
    float base = 2.0f;
    float thick = 0.03f;
    float cap = 0.6f;
    float steps = 8.0f;
    float strength = 1.0f;
    float pad[11]{};
};
struct RcParams {
    float rays = 8.0f;
    float spacing = 0.45f;
    float intensity = 1.0f;
    float maxDist = 48.0f;
    float cap = 0.6f;
    float pad[11]{};
};
struct ShadeParams {
    float sscsReach = 30.0f;
    float sscsThick = 1.0f;
    float sscsSteps = 20.0f;
    float sscsPx = 128.0f;
    float fogDensity = 0.012f;
    float fogHeight = 4.0f;
    float fogScatter = 1.0f;
    float sscsAmt = 1.0f;
    float specOcc = 1.0f;
    float toksvig = 1.0f;
    float pom = 0.35f;
    float ggx = 1.0f;
    float gtaoAmt = 0.55f;
    float lamp = 1.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};
struct PostPack {
    float post[12]{};
    float pad[4]{};
};
struct BloomParams {
    float threshold = 0.35f;
    uint32_t tonemapper = 0;
    uint32_t padU = 0;
    float grain = 0.0f;
    float bloomMix = 0.15f;
    float vignette = 1.1f;
    float ca = 2.0f;
    float pad[9]{};
};
struct FramePost {
    PassFlags flags{};
    GtaoParams gtao{};
    SsrParams ssr{};
    SsrcParams ssrc{};
    RcParams rc{};
    ShadeParams shade{};
    PostPack sliders{};
    BloomParams bloom{};
};

static_assert(sizeof(FrameView) == 2240);
static_assert(sizeof(FrameView) % 64 == 0);
static_assert(sizeof(PassFlags) == 64);
static_assert(sizeof(GtaoParams) == 64);
static_assert(sizeof(SsrParams) == 64);
static_assert(sizeof(SsrcParams) == 64);
static_assert(sizeof(RcParams) == 64);
static_assert(sizeof(ShadeParams) == 64);
static_assert(sizeof(PostPack) == 64);
static_assert(sizeof(BloomParams) == 64);
static_assert(sizeof(FramePost) == 512);
static_assert(sizeof(FramePost) % 64 == 0);
static_assert(offsetof(FramePost, gtao) == 64);
static_assert(offsetof(FramePost, ssr) == 128);
static_assert(offsetof(FramePost, ssrc) == 192);
static_assert(offsetof(FramePost, rc) == 256);
static_assert(offsetof(FramePost, shade) == 320);
static_assert(offsetof(FramePost, sliders) == 384);
static_assert(offsetof(FramePost, bloom) == 448);

struct GpuMeshlet {
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    float coneAxis[3]{};
    float coneCutoff = -1.0f;
};

struct MeshletGpu {
    GpuBuffer verts;
    GpuBuffer indices;
    GpuBuffer frame;
};

[[nodiscard]] bool meshletGpuCreate(MeshletGpu& s, Device& d);
void meshletGpuDestroy(MeshletGpu& s, Device& d);
void meshletGpuWriteFrame(MeshletGpu& s, VkExtent2D extent);

} // namespace burnhope
