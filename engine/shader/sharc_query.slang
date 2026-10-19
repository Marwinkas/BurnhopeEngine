#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "core/atmosphere.slang"
#include "sharc_params.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] globallycoherent RWByteAddressBuffer gAcc;
[[vk::binding(72, 0)]] RWStructuredBuffer<uint4> gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;
[[vk::binding(74, 0)]] RWTexture2D<float4> gIndirect;

#include "sharc_cache.slang"
#include "sharc_hit.slang"

// Один косинусный луч из точки кадра. В картинку попадает только SharcGetCachedRadiance,
// без неба и без прямого света вторичного хита. Альбедо приёмника накладывает сборка.

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcQuery(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    if (gVis.Load(int3(px, 0)) == 0u || (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u)) {
        gIndirect[px] = 0.0;
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        gIndirect[px] = 0.0;
        return;
    }
    float3 position = reconstructWorld(px, depth);
    float3 normal = octDecode(gSurf1[px].xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    } else {
        normal = normalize(normal);
    }
    float3 view = normalize(gFrame.cameraPos.xyz - position);
    if (dot(normal, view) < 0.0) {
        normal = -normal;
    }
    uint rng = px.x * 1973u ^ px.y * 9277u ^ gSharc.frameIndex * 26699u ^ 0x85ebca6bu;
    float2 u = float2(sharcRand(rng), sharcRand(rng));
    float3 direction = sharcCosine(normal, u);
    float hitT = 0.0;
    uint prim = 0u;
    float2 bary = 0.0;
    float3 origin = position + normal * 0.02;
    if (!sharcTrace(origin, direction, 0.05, 1000.0, hitT, prim, bary)) {
        gIndirect[px] = 0.0;
        return;
    }
    float3 hitP = origin + direction * hitT;
    SharcSurf hit = sharcSurface(prim, bary, hitP, -direction);
    if (!hit.hit) {
        gIndirect[px] = 0.0;
        return;
    }
    SharcHitData sharcHit;
    sharcHit.positionWorld = hitP;
    sharcHit.normalWorld = hit.geoNormal;
    sharcHit.materialDemodulation = hit.demod;
    float3 cached = 0.0;
    if (!sharcGetCachedRadiance(sharcHit, sharcGrid(), cached)) {
        gIndirect[px] = 0.0;
        return;
    }
    gIndirect[px] = float4(max(cached.r, 0.0), max(cached.g, 0.0), max(cached.b, 0.0), 1.0);
}
