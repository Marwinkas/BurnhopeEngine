#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"
#include "rc_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(15, 0)]] Texture2D<float> gShadow0;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;

#include "shade_shadow.slang"
#include "rc_surface.slang"

// Split RC, как у Chandra.
// Луч несёт исходящую яркость J и пропускание β.
// Попадание: J = альбедо * (солнце * карта теней * N·L + прошлый отскок), β = 0.
// Пустой интервал: J = 0, β = 1, дальний свет проходит.
// Промах последнего каскада: только небо, β = 0.
// Несколько лучей одного бина усредняют β: ближе к предмету доля попаданий выше, тень темнее.

float4 rcPos(uint vertex) {
    const uint byteOff = rcPosByte() + vertex * 16u;
    return float4(asfloat(rcLoad(byteOff)), asfloat(rcLoad(byteOff + 4u)), asfloat(rcLoad(byteOff + 8u)), asfloat(rcLoad(byteOff + 12u)));
}

float3 rcSky(float3 dir) {
    const float3 s = gSkyView.SampleLevel(gClamp, atmSkyUv(dir), 0.0).rgb;
    return min(max(s, 0.0), float3(8.0, 8.0, 8.0));
}

float3 rcIrrTexel(uint probe, uint x, uint y) {
    const uint off = sliceBase() + rcIrrRel() + probe * rcIrrStride() + (y * 6u + x) * 16u;
    return float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
}

float3 rcBounce(uint probe, float3 N) {
    const float2 octTexel = octEncode(N) * 4.0 + 1.0;
    const float2 p = clamp(octTexel - 0.5, 0.0, 4.999);
    const uint2 i = uint2(p);
    const float2 f = frac(p);
    const uint2 i1 = min(i + 1u, uint2(5u, 5u));
    const float3 a = rcIrrTexel(probe, i.x, i.y);
    const float3 b = rcIrrTexel(probe, i1.x, i.y);
    const float3 c = rcIrrTexel(probe, i.x, i1.y);
    const float3 d = rcIrrTexel(probe, i1.x, i1.y);
    return max(lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y), 0.0);
}

float3 rcSunDir() {
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    return dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
}

float3 rcSunColor() {
    float3 sunCol = gSun.sunColor.rgb;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.97, 0.92);
    }
    return sunCol;
}

float rcSunIntensity() {
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    return inten;
}

float rcReach() {
    return max(max(gRcParams.maxDist, 16.0), 4.0);
}

uint rcRayCount() {
    return (uint)clamp(gRcParams.rays + 0.5, 1.0, 4.0);
}

// Та же видимость, что у прямого солнца. Луч к солнцу из BLAS её не повторял.
float rcSunVis(float3 P, float3 N, float ndl) {
    if (ndl <= 0.01) {
        return 0.0;
    }
    if (gFrame.disableShadow != 0u) {
        return 1.0;
    }
    const float slope = sqrt(saturate(1.0 - ndl * ndl));
    const float3 p0 = P + N * max(gSun.sunSplit[0], 1.0) * 2.0 / 4096.0 * slope;
    const float o0 = cascadeOutside(0u, p0);
    if (o0 <= 0.0) {
        const float w = saturate(-o0 / 0.1);
        float shadow = cascadeShadow(0u, p0);
        if (w < 0.999) {
            const float3 p1 = P + N * max(gSun.sunSplit[1], 1.0) * 2.0 / 4096.0 * slope;
            shadow = lerp(cascadeShadow(1u, p1), shadow, w);
        }
        return shadow;
    }
    const float3 p1 = P + N * max(gSun.sunSplit[1], 1.0) * 2.0 / 4096.0 * slope;
    const float o1 = cascadeOutside(1u, p1);
    if (o1 <= 0.0) {
        const float w = saturate(-o1 / 0.1);
        float shadow = cascadeShadow(1u, p1);
        if (w < 0.999) {
            const float3 p2 = P + N * max(gSun.sunSplit[2], 1.0) * 2.0 / 4096.0 * slope;
            shadow = lerp(cascadeShadow(2u, p2), shadow, w);
        }
        return shadow;
    }
    const float3 p2 = P + N * max(gSun.sunSplit[2], 1.0) * 2.0 / 4096.0 * slope;
    return cascadeOutside(2u, p2) <= 0.0 ? cascadeShadow(2u, p2) : cascadeShadowCover(2u, p2);
}

float3 rcHemisphere(float2 u, float3 N) {
    const float z = u.y * 2.0 - 1.0;
    const float rad = sqrt(max(1.0 - z * z, 0.0));
    const float phi = u.x * 6.28318530718;
    float3 dir = float3(rad * cos(phi), rad * sin(phi), z);
    if (dot(dir, N) < 0.0) {
        dir = -dir;
    }
    return dir;
}

uint rcHitCascade(float hitT, float t0, bool miss) {
    if (miss) {
        return 3u;
    }
    if (hitT <= t0) {
        return 0u;
    }
    if (hitT <= t0 * 4.0) {
        return 1u;
    }
    if (hitT <= t0 * 16.0) {
        return 2u;
    }
    return 3u;
}

void rcDepositChain(uint probe, uint lod, float3 dir, float hitT, float3 Le, bool miss) {
    if (probe == kInvalid) {
        return;
    }
    const float t0 = max(rcT0(lod), 0.04);
    const uint hitC = rcHitCascade(hitT, t0, miss);
    uint cursor = probe;
    [unroll]
    for (uint k = 0u; k < 4u; ++k) {
        if (k > hitC || cursor == kInvalid) {
            break;
        }
        const bool atHit = k == hitC;
        const float3 J = atHit ? Le : float3(0.0, 0.0, 0.0);
        const float beta = atHit ? 0.0 : 1.0;
        rcDeposit(k, cursor, rcDirIndex(dir, k), J, beta);
        if (k == 0u) {
            rcStore(rcMetaAddr(0u, cursor) + 24u, 1u);
        }
        if (k < 3u) {
            cursor = rcParent(k, cursor);
        }
    }
}

void rcReadShaded(uint2 px, float depth, out float3 lit, out float3 albedo, out float metal, out float3 shadingN) {
    lit = max(gHdr[px].rgb, 0.0);
    albedo = max(gSurf0[px].rgb, 0.0);
    metal = saturate(gSurf1[px].z);
    shadingN = octDecode(gSurf1[px].xy);
    if (dot(shadingN, shadingN) < 0.5) {
        shadingN = reconstructNormalFromDepth(gDepth, px, reconstructWorld(px, depth));
    }
}

bool rcShadeView() {
    return gFrame.debugMode == 0u || gFrame.debugMode == 4u;
}

// Пиксель уже содержит PBR: нормаль карты, микротень, каскад солнца, лампы.
// Окно 5×5 и допуск по глубине вида, иначе точка луча промахивается на тексель
// и каскад снова считает плоский треугольник.
bool rcScreenLit(float3 hitP, out float3 lit, out float3 albedo, out float metal, out float3 shadingN) {
    lit = 0.0;
    albedo = float3(0.5, 0.5, 0.5);
    metal = 0.0;
    shadingN = float3(0.0, 1.0, 0.0);
    if (!rcShadeView()) {
        return false;
    }
    const float4 clip = worldToClip(hitP);
    if (clip.w <= 0.001) {
        return false;
    }
    const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (uv.x < -0.02 || uv.y < -0.02 || uv.x > 1.02 || uv.y > 1.02) {
        return false;
    }
    const int2 center = int2(uv * float2(gFrame.extent.xy));
    const float tol = max(0.45, clip.w * 0.05);
    float best = tol;
    bool found = false;
    [loop]
    for (int dy = -2; dy <= 2; ++dy) {
        [loop]
        for (int dx = -2; dx <= 2; ++dx) {
            const int2 ip = center + int2(dx, dy);
            if (ip.x < 0 || ip.y < 0 || ip.x >= int(gFrame.extent.x) || ip.y >= int(gFrame.extent.y)) {
                continue;
            }
            const uint2 px = uint2(ip);
            float depth = 0.0;
            if (!rcPixel(px, depth)) {
                continue;
            }
            const float3 surf = reconstructWorld(px, depth);
            const float err = length(surf - hitP);
            const float viewSurf = worldToClip(surf).w;
            if (err >= best || abs(viewSurf - clip.w) > tol) {
                continue;
            }
            best = err;
            rcReadShaded(px, depth, lit, albedo, metal, shadingN);
            found = true;
        }
    }
    return found;
}

// Луч BLAS промахнулся или сел не в тот тексель. Шаг по глубине экрана
// берёт тот пиксель, который камера уже закрасила.
bool rcMarchShaded(float3 origin, float3 dir, float reach, out float hitT, out float3 lit, out float3 albedo, out float metal, out float3 shadingN) {
    hitT = reach;
    lit = 0.0;
    albedo = float3(0.5, 0.5, 0.5);
    metal = 0.0;
    shadingN = float3(0.0, 1.0, 0.0);
    if (!rcShadeView()) {
        return false;
    }
    float t = 0.06;
    [loop]
    for (uint s = 0u; s < 14u; ++s) {
        t *= 1.55;
        if (t > reach) {
            return false;
        }
        const float3 Q = origin + dir * t;
        const float4 clip = worldToClip(Q);
        if (clip.w <= 0.001) {
            return false;
        }
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
            return false;
        }
        const uint2 px = min(uint2(uv * float2(gFrame.extent.xy)), gFrame.extent - 1u);
        float depth = 0.0;
        if (!rcPixel(px, depth)) {
            continue;
        }
        const float3 surf = reconstructWorld(px, depth);
        const float along = dot(surf - origin, dir);
        if (along < 0.05 || along > t) {
            continue;
        }
        const float side = length(surf - origin - dir * along);
        if (side > max(0.1, along * 0.025)) {
            continue;
        }
        hitT = along;
        rcReadShaded(px, depth, lit, albedo, metal, shadingN);
        return true;
    }
    return false;
}

// Исходящий свет поверхности. На экране это готовый пиксель. За экраном — солнце и карта теней.
float3 rcBounceAt(float3 hitP, float3 n) {
    if (gPush.cascadeIndex == 0u) {
        return 0.0;
    }
    const uint hitLod = rcLod(hitP);
    const uint hp = rcLookup(0u, rcPackKey(rcCell(hitP, 0u, hitLod), hitLod));
    if (hp == kInvalid || rcLoad(rcMetaAddr(0u, hp) + 24u) == 0u) {
        return 0.0;
    }
    return rcBounce(hp, n);
}

float3 rcLeShaded(float3 hitP, float3 lit, float3 albedo, float metal, float3 shadingN) {
    const float3 bounceIn = rcBounceAt(hitP, shadingN);
    return min(max(lit + albedo * (1.0 - metal) * bounceIn, 0.0), kRadMax);
}

float3 rcLeOffscreen(float3 hitP, float3 n, float3 albedo, float metal, float emissive) {
    const float3 sun = rcSunDir();
    const float ndl = saturate(dot(n, sun));
    const float vis = rcSunVis(hitP, n, ndl);
    const float3 bounceIn = rcBounceAt(hitP, n);
    const float3 incoming = ndl * vis * rcSunColor() * rcSunIntensity() * 0.318309886 + bounceIn;
    float3 Le = albedo * (1.0 - metal) * incoming;
    Le += min(albedo * emissive * 4.0, kRadMax);
    return min(max(Le, 0.0), kRadMax);
}

RaytracingAccelerationStructure rcScene() {
    return RaytracingAccelerationStructure(uint64_t(rcLoad(112u)) | (uint64_t(rcLoad(116u)) << 32));
}

void rcCast(float3 origin, float3 dir, float reach, out float hitT, out float3 Le, out bool miss) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.01;
    ray.TMax = reach;
    q.TraceRayInline(rcScene(), RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
    q.Proceed();
    hitT = reach;
    Le = rcSky(dir);
    miss = true;
    const bool hw = q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
    float hwT = reach;
    float3 hwP = origin;
    float3 hwN = -dir;
    float3 hwAlbedo = float3(0.5, 0.5, 0.5);
    float hwMetal = 0.0;
    float hwEmissive = 0.0;
    if (hw) {
        hwT = q.CommittedRayT();
        hwP = origin + dir * hwT;
        const uint prim = q.CommittedPrimitiveIndex();
        const float4 a = rcPos(prim * 3u);
        const float3 b = rcPos(prim * 3u + 1u).xyz;
        const float3 c = rcPos(prim * 3u + 2u).xyz;
        hwN = cross(b - a.xyz, c - a.xyz);
        hwN = dot(hwN, hwN) > 1e-12 ? normalize(hwN) : -dir;
        if (dot(hwN, dir) > 0.0) {
            hwN = -hwN;
        }
        uint matCount;
        uint matStride;
        gMat.GetDimensions(matCount, matStride);
        const uint matId = asuint(a.w);
        MaterialGpu mat;
        mat.baseColor = float4(0.5, 0.5, 0.5, 1.0);
        mat.roughness = 1.0;
        mat.metallic = 0.0;
        mat.emissive = 0.0;
        if (matCount > 0u) {
            mat = gMat[min(matId, matCount - 1u)];
        }
        hwAlbedo = mat.baseColor.rgb;
        hwMetal = saturate(mat.metallic);
        hwEmissive = mat.emissive;
        float3 lit;
        float3 albedo;
        float metal;
        float3 shadingN;
        if (rcScreenLit(hwP, lit, albedo, metal, shadingN)) {
            miss = false;
            hitT = hwT;
            Le = rcLeShaded(hwP, lit, albedo, metal, shadingN);
            return;
        }
    }
    float marchT;
    float3 lit;
    float3 albedo;
    float metal;
    float3 shadingN;
    if (rcMarchShaded(origin, dir, reach, marchT, lit, albedo, metal, shadingN) && (!hw || marchT <= hwT + 0.2)) {
        miss = false;
        hitT = marchT;
        Le = rcLeShaded(origin + dir * marchT, lit, albedo, metal, shadingN);
        return;
    }
    if (!hw) {
        return;
    }
    miss = false;
    hitT = hwT;
    Le = rcLeOffscreen(hwP, hwN, hwAlbedo, hwMetal, hwEmissive);
}

void rcShoot(float3 P, float3 N, float3 dir) {
    if (dot(dir, N) <= 0.0) {
        return;
    }
    float hitT;
    float3 Le;
    bool miss;
    rcCast(P + N * 0.01, dir, rcReach(), hitT, Le, miss);
    const uint lod0 = rcLod(P);
    const uint lod1 = min(lod0 + 1u, 5u);
    rcDepositChain(rcLookup(0u, rcPackKey(rcCell(P, 0u, lod0), lod0)), lod0, dir, hitT, Le, miss);
    if (lod1 != lod0) {
        rcDepositChain(rcLookup(0u, rcPackKey(rcCell(P, 0u, lod1), lod1)), lod1, dir, hitT, Le, miss);
    }
}

bool rcSurface(uint2 px, out float3 P, out float3 N) {
    P = 0.0;
    N = float3(0.0, 1.0, 0.0);
    float depth = 0.0;
    if (!rcPixel(px, depth)) {
        return false;
    }
    P = reconstructWorld(px, depth);
    N = octDecode(gSurf1[px].xy);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    }
    N = orientNormal(N, gFrame.cameraPos.xyz - P);
    return true;
}

// 32 направления якоря. Полусфера зонда заполнена, пустой бин не становится небом.
[shader("compute")]
[numthreads(64, 1, 1)]
void rcTrace(uint3 dtid : SV_DispatchThreadID) {
    const uint dirId = dtid.x & 31u;
    const uint probe = dtid.x >> 5u;
    if (probe >= min(rcCount(0u), rcProbeCap(0u))) {
        return;
    }
    const uint meta = rcMetaAddr(0u, probe);
    const uint score = rcLoad(meta + 28u);
    const uint width = gFrame.extent.x;
    if (score == 0xFFFFFFFFu || width == 0u) {
        return;
    }
    const uint2 px = uint2(score % width, score / width);
    float3 P;
    float3 N;
    if (!rcSurface(px, P, N)) {
        return;
    }
    if (dirId == 0u) {
        rcStore(meta + 24u, 1u);
    }
    const float3 dir = rcDecodeGrid(0u, dirId);
    rcShoot(P, N, dir);
}

// Один стабильный луч R2 на пиксель и на слот слайдера. Лучи клетки падают в одни бины,
// поэтому β — доля промахов: у предмета тень плотнее, дальше родитель просвечивает.
[shader("compute")]
[numthreads(8, 8, 1)]
void rcTracePixels(uint3 dtid : SV_DispatchThreadID) {
    float3 P;
    float3 N;
    if (!rcSurface(dtid.xy, P, N)) {
        return;
    }
    const uint nRays = rcRayCount();
    const uint pix = dtid.y * gFrame.extent.x + dtid.x;
    [loop]
    for (uint r = 0u; r < nRays; ++r) {
        rcShoot(P, N, rcHemisphere(rcR2(pix * nRays + r), N));
    }
}
