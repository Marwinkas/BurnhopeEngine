import common.Bindless;

static const uint VIRTUAL_PAGES_X = 4096;
static const uint VIRTUAL_PAGES_Y = 4096;
static const float PAGE_SIZE = 128.0;
static const float PHYSICAL_ATLAS_SIZE = 4096.0;

static const float2 poissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

/** Simple single-tap VSM (vsm_sample.glsl). Returns 1.0 = lit. */
export float SampleVirtualShadowMapSimple(
    float3 projCoords,
    Texture2D<float> vsmAtlas,
    StructuredBuffer<uint> virtualPages) {
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }
    uint vPageX = uint(projCoords.x * float(VIRTUAL_PAGES_X));
    uint vPageY = uint(projCoords.y * float(VIRTUAL_PAGES_Y));
    uint vPageIdx = vPageY * VIRTUAL_PAGES_X + vPageX;
    uint physPage = virtualPages[vPageIdx];
    if (physPage == 0xFFFFFFFF || physPage == 0xFFFFFFFE) {
        return 1.0;
    }
    float2 pageUV = frac(projCoords.xy * float2(VIRTUAL_PAGES_X, VIRTUAL_PAGES_Y));
    uint physX = physPage % 32;
    uint physY = physPage / 32;
    float2 physTexelCoord = float2(physX * PAGE_SIZE, physY * PAGE_SIZE) + pageUV * PAGE_SIZE;
    float2 atlasUV = physTexelCoord / PHYSICAL_ATLAS_SIZE;
    float shadowDepth = vsmAtlas.SampleLevel(heapSampler(0u), atlasUV, 0).r;
    float bias = max(0.005 * (1.0 - projCoords.z), 0.0005);
    return (projCoords.z - bias > shadowDepth) ? 0.0 : 1.0;
}

/** PCF VSM used by lighting / volumetric (16 taps). Returns shadow factor 0..1. */
export float SampleVirtualShadowMapPCF(
    float3 projCoords,
    uint lightIndex,
    uint face,
    int encodedSlot,
    uint2 dispatchXY,
    Texture2D<float> vsmAtlas,
    StructuredBuffer<uint> virtualPages,
    Texture2D<float> noiseTex,
    SamplerState samp) {
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    float pagesPerFace = float(encodedSlot % 10000) / 128.0;
    uint slotIndex = lightIndex * 6 + face;
    uint offsetX = (slotIndex % 32) * 128;
    uint offsetY = (slotIndex / 32) * 128;
    float2 texelSize = float2(1.0 / PHYSICAL_ATLAS_SIZE, 1.0 / PHYSICAL_ATLAS_SIZE);
    float2 noiseUV = (float2(dispatchXY) / 256.0) + float2(float(encodedSlot + face) * 0.137);
    float randomAngle = noiseTex.SampleLevel(samp, noiseUV, 0).r * 6.2831853;
    float s = sin(randomAngle);
    float c = cos(randomAngle);
    float2x2 rot = float2x2(c, -s, s, c);
    float shadow = 0.0;
    float bias = max(0.005 * (1.0 - projCoords.z), 0.0005);
    for (int i = 0; i < 16; ++i) {
        float2 offset = mul(rot, poissonDisk[i]) * 3.0 * texelSize;
        float2 sampleCoords = clamp(projCoords.xy + offset, float2(0.0), float2(0.99999));
        float2 pUV = frac(sampleCoords * pagesPerFace);
        uint localX = min(uint(sampleCoords.x * pagesPerFace), uint(pagesPerFace - 1.0));
        uint localY = min(uint(sampleCoords.y * pagesPerFace), uint(pagesPerFace - 1.0));
        uint pvX = offsetX + localX;
        uint pvY = offsetY + localY;
        uint physPage = virtualPages[pvY * VIRTUAL_PAGES_X + pvX];
        if (physPage != 0xFFFFFFFF && physPage != 0xFFFFFFFE) {
            uint pX = physPage % 32;
            uint pY = physPage / 32;
            float2 physTexelCoord = float2(pX * PAGE_SIZE, pY * PAGE_SIZE) + pUV * PAGE_SIZE;
            float shadowDepth = vsmAtlas.SampleLevel(samp, physTexelCoord / PHYSICAL_ATLAS_SIZE, 0).r;
            shadow += (projCoords.z - bias > shadowDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 16.0;
}
