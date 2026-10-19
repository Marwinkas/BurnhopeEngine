#pragma once

#include "gpu_scene/Mat4.hpp"

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct FrameView {
    float viewProj[16];
    float invViewProj[16];
    float cameraPos[4];
    float invExtent[2];
    float padInv[2];
    uint32_t extent[2];
    uint32_t padExt[2];
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
    uint32_t debugAlign[4]{};
    float freezeVp[16]{};
    float viewTail[16]{};
    float viewPad[4]{};
};

struct FrameSun {
    float sunDirIntensity[4]{};
    float sunColor[4]{};
    float pointPos[3][4]{};
    float pointColor[3][4]{};
    float sunCascade[3][16]{};
    float sunSplit[4]{};
    float pointFace[3][6][16]{};
    float cubeFace[6][16]{};
    float cubeMin[4]{};
    float cubeMax[4]{};
    float pad[4]{};
};

// Каждый блок 64 байта и свой слот кучи. Поле внутри SSR не сдвигает GTAO и не сдвигает солнце.
struct PassFlags {
    int32_t forceLod = -1;
    uint32_t passMask = 63u;
    uint32_t lightProfile = 0;
    uint32_t pad[13]{};
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
    float rays = 1.0f;
    float spacing = 0.2f;
    float intensity = 1.0f;
    float maxDist = 48.0f;
    float cap = 0.6f;
    float sunScale = 1.0f;
    float bounce = 1.0f;
    float fogFalloff = 0.08f;
    float fogR = 0.62f;
    float fogG = 0.72f;
    float fogB = 0.86f;
    float fogLum = 1.4f;
    float fogStart = 0.0f;
    float fogOpacity = 0.92f;
    float fogInStart = 0.0f;
    float fogInSize = 40.0f;
};
struct ShadeParams {
    float sscsReach = 0.2f;
    float sscsThick = 0.02f;
    float sscsSteps = 12.0f;
    float sscsPx = 128.0f;
    float fogDensity = 0.002f;
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
struct GradeBlock {
    float prev0[4]{};
    float prev1[4]{};
    float prev2[4]{};
    float prev3[4]{};
    float mixR[4]{1.0f, 0.0f, 0.0f, 0.0f};
    float mixG[4]{0.0f, 1.0f, 0.0f, 0.0f};
    float mixB[4]{0.0f, 0.0f, 1.0f, 0.0f};
    float shadows[4]{1.0f, 1.0f, 1.0f, 0.0f};
    float midtones[4]{1.0f, 1.0f, 1.0f, 0.0f};
    float highlights[4]{1.0f, 1.0f, 1.0f, 0.0f};
    float slope[4]{1.0f, 1.0f, 1.0f, 0.0f};
    float offset[4]{};
    float power[4]{1.0f, 1.0f, 1.0f, 0.0f};
    float ranges[4]{0.0f, 0.333f, 0.55f, 1.0f};
    float generic[4]{1.0f, 0.18f, 0.18f, 10.0f};
    float curves[4]{1.0f, 1.0f, 1.0f, 1.0f};
};
static_assert(sizeof(GradeBlock) == 256);

struct PostPack {
    float exposure = 0.0f;
    float temperature = 0.0f;
    float tint = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float night = 0.0f;
    float shadows = 1.0f;
    float midtones = 1.0f;
    float highlights = 1.0f;
    float slope = 1.0f;
    float cdlOffset = 0.0f;
    float power = 1.0f;
    float dofFocus = 8.0f;
    float dofAmount = 0.0f;
    float flare = 0.0f;
    float flags = 0.0f;
};
// Свой блок, 192 байта, binding 58. В FrameView и в соседние ползунки не входит.
struct AtmosphereParams {
    float sunZenith = 35.0f;
    float sunAzimuth = 30.0f;
    float sunIntensity = 100000.0f;
    float sunDisk = 1.0f;
    float rayleigh = 1.0f;
    float mie = 1.0f;
    float mieG = 0.8f;
    float ozone = 1.0f;
    float cloudCoverage = 0.42f;
    float cloudDensity = 0.08f;
    float cloudBottom = 1500.0f;
    float cloudTop = 4000.0f;
    float cloudSpeedX = 12.0f;
    float cloudSpeedZ = 4.0f;
    float cloudDetail = 0.35f;
    float moon = 0.15f;
    float stars = 1.0f;
    float nightAmbient = 0.02f;
    float time = 0.0f;
    float parity = 0.0f;
    float reserved0 = 0.0f;
    float reserved1 = 0.0f;
    float pad[26]{};
};

struct BloomParams {
    float threshold = 0.35f;
    uint32_t tonemapper = 0;
    uint32_t padU = 0;
    float grain = 0.0f;
    float bloomMix = 0.15f;
    float vignette = 0.35f;
    float ca = 2.0f;
    float cas = 0.25f;
    float pad[8]{};
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

static_assert(sizeof(FrameView) == 384);
static_assert(sizeof(FrameView) % 64 == 0);
static_assert(offsetof(FrameView, debugMode) == 176);
static_assert(offsetof(FrameView, freezeVp) == 240);
static_assert(sizeof(FrameSun) == 1920);
static_assert(sizeof(FrameSun) % 64 == 0);
static_assert(sizeof(PassFlags) == 64);
static_assert(sizeof(GtaoParams) == 64);
static_assert(sizeof(SsrParams) == 64);
static_assert(sizeof(SsrcParams) == 64);
static_assert(sizeof(RcParams) == 64);
static_assert(sizeof(ShadeParams) == 64);
static_assert(sizeof(PostPack) == 64);
static_assert(sizeof(AtmosphereParams) == 192);
static_assert(sizeof(AtmosphereParams) % 64 == 0);
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


} // namespace burnhope
