// Fullscreen resolve. One R32 vis pixel: 20b instance, 12b primitive.
// MaterialGpuData is 64 bytes and matches core/gfx/Material.hpp.

struct MaterialGpuData {
    float4 baseColor;
    float roughness;
    float metallic;
    float emissiveIntensity;
    uint flags;
    uint albedoTex;
    uint normalTex;
    uint ormTex;
    uint emissiveTex;
    uint4 pad;
};

struct ResolveInstance {
    float4 world0;
    float4 world1;
    float4 world2;
    float4 sphere;
    uint materialId;
    uint meshId;
    uint2 pad;
};

struct ResolvePush {
    ResolveInstance* instances;
    MaterialGpuData* materials;
    uint instanceCount;
    uint materialCount;
};

[[vk::push_constant]]
ResolvePush gResolve;

[[vk::binding(0, 0)]]
Texture2D<uint> gVis;

[[vk::binding(1, 0)]]
Texture2D<float> gShadow;

[[vk::binding(2, 0)]]
RWTexture2D<float4> gHdr;

uint visInstance(uint pixel) { return pixel & 0xFFFFF; }
uint visPrimitive(uint pixel) { return pixel >> 20; }

float pcf3x3(Texture2D<float> shadowMap, int2 texel, float receiver) {
    float lit = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float occluder = shadowMap.Load(int3(texel + int2(x, y), 0));
            lit += receiver <= occluder ? 1.0 : 0.0;
        }
    }
    return lit / 9.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void resolveCs(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy;
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0 || gResolve.instances == nullptr || gResolve.materials == nullptr) {
        gHdr[px] = float4(0.07, 0.09, 0.12, 1.0);
        return;
    }
    const uint instanceId = visInstance(vis);
    const uint primitiveId = visPrimitive(vis);
    if (instanceId >= gResolve.instanceCount) {
        return;
    }
    const ResolveInstance inst = gResolve.instances[instanceId];
    if (inst.materialId >= gResolve.materialCount) {
        return;
    }
    const MaterialGpuData mat = gResolve.materials[inst.materialId];
    const float shadow = pcf3x3(gShadow, int2(px) & 2047, 0.5);
    const float shade = 0.15 + 0.85 * shadow;
    gHdr[px] = float4(mat.baseColor.rgb * shade, 1.0 + float(primitiveId) * 0.0);
}
