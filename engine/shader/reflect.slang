// Гибрид: FFX-SSSR по MAX Hi-Z, на промахе один непрозрачный луч в TLAS.
// Попадание в экран берёт готовый HdrA (PBR, ReSTIR, SHaRC). Вне экрана — тот же прямой свет, что и SHaRC, плюс кэш.
// Промах луча — купол неба. Вторичных лучей нет. Y не переворачивается.

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "core/hiz_atlas.slang"
#include "core/atmosphere.slang"
#include "sharc_params.slang"

[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] globallycoherent RWByteAddressBuffer gAcc;
[[vk::binding(72, 0)]] RWByteAddressBuffer gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;

#include "sharc_cache.slang"
#include "sharc_hit.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(6, 0)]] Texture2D<float> gHiZ0;
[[vk::binding(31, 0)]] Texture2D<float> gHiZ1;
[[vk::binding(15, 0)]] RWStructuredBuffer<uint2> gRayStat;
[[vk::binding(36, 0)]] RWTexture2D<float4> gReflect;
[[vk::binding(37, 0)]] RWTexture2D<float4> gHist0;
[[vk::binding(38, 0)]] RWTexture2D<float4> gHist1;
[[vk::binding(60, 0)]] ConstantBuffer<GradeBlock> gBlock;

struct FlightPush {
    uint flight;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
FlightPush gPush;

static const float kSssrFloatMax = 3.402823466e+38;
static const float kSssrMirror = 0.06;
static const float kSssrThickness = 0.60;
static const int kSssrMaxMip = 5;
static const int2 kSssrPrefilter[15] = {
    int2(0, 1), int2(-2, 1), int2(2, -3), int2(-3, 0), int2(1, 2), int2(-1, -2), int2(3, 0), int2(-3, 3),
    int2(0, -3), int2(-1, -1), int2(2, 1), int2(-2, -2), int2(1, 0), int2(0, 2), int2(3, -1)
};

float3 sssrSafeRcp(float3 v) {
    return float3(
        abs(v.x) > 1e-8 ? 1.0 / v.x : kSssrFloatMax,
        abs(v.y) > 1e-8 ? 1.0 / v.y : kSssrFloatMax,
        abs(v.z) > 1e-8 ? 1.0 / v.z : kSssrFloatMax);
}

float sssrLoadDepth(float2 mipPos, int mip) {
    const int level = clamp(mip, 0, kSssrMaxMip);
    const uint2 ext = max(gFrame.extent.xy >> uint(level), uint2(1, 1));
    if (mipPos.x < 0.0 || mipPos.y < 0.0 || mipPos.x >= float(ext.x) || mipPos.y >= float(ext.y)) {
        return 0.0;
    }
    const int2 p = int2(mipPos);
    if (gFrame.disableHiZ != 0u) {
        const int2 full = int2(gFrame.extent.xy) - int2(1, 1);
        const int2 q = clamp(p << level, int2(0, 0), full);
        return gDepth.Load(int3(q, 0));
    }
    const int y = int(atlasMipY(uint(level), gFrame.extent.y));
    const int3 at = int3(p.x, y + p.y, 0);
    return gPush.flight == 0u ? gHiZ0.Load(at) : gHiZ1.Load(at);
}

float3 sssrLoadNormal(int2 pixel) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 p = clamp(pixel, int2(0, 0), ext);
    float3 n = octDecode(gSurf1[p].xy);
    if (dot(n, n) < 0.5) {
        const float depth = gDepth.Load(int3(p, 0));
        n = reconstructNormalFromDepth(gDepth, uint2(p), reconstructWorld(uint2(p), depth));
    }
    return normalize(n);
}

float3 sssrProject(float3 world) {
    const float4 clip = worldToClip(world);
    float3 p = clip.xyz / max(clip.w, 1e-6);
    p.xy = p.xy * 0.5 + 0.5;
    return p;
}

float sssrBlue(int2 pixel, uint salt) {
    const uint frame = gSharc.frameIndex;
    const int2 p = pixel + int2(int(frame & 255u) * 5, int((frame >> 8u) & 255u) * 3) + int2(int(salt), int(salt * 3u));
    return frac(52.9829189 * frac(0.06711056 * float(p.x) + 0.00583715 * float(p.y)));
}

float3 sssrSampleGgxVndf(float3 Ve, float alpha, float u1, float u2) {
    const float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    const float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    const float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
    const float3 T2 = cross(Vh, T1);
    const float r = sqrt(saturate(u1));
    const float phi = 6.28318530718 * u2;
    const float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    const float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(max(0.0, 1.0 - t1 * t1)) + s * t2;
    const float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
    return normalize(float3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}

float3 sssrReflect(float3 viewFromCam, float3 N, float roughness, int2 pixel) {
    float3 U;
    if (abs(N.z) > 0.0) {
        const float k = max(sqrt(N.y * N.y + N.z * N.z), 1e-6);
        U = float3(0.0, -N.z / k, N.y / k);
    } else {
        const float k = max(sqrt(N.x * N.x + N.y * N.y), 1e-6);
        U = float3(N.y / k, -N.x / k, 0.0);
    }
    const float3 B = cross(N, U);
    const float3 Ve = float3(dot(viewFromCam, U), dot(viewFromCam, B), dot(viewFromCam, N));
    const float2 u = float2(sssrBlue(pixel, 0u), sssrBlue(pixel, 17u));
    const float alpha = roughness <= kSssrMirror ? 0.0 : roughness * roughness;
    float3 Ne = sssrSampleGgxVndf(Ve, max(alpha, 1e-4), u.x, u.y);
    if (roughness <= kSssrMirror) {
        Ne = float3(0.0, 0.0, 1.0);
    }
    const float3 reflected = reflect(-Ve, Ne);
    return reflected.x * U + reflected.y * B + reflected.z * N;
}

void sssrInitialAdvance(float3 origin, float3 direction, float3 invDirection, float2 mipRes, float2 mipInv, float2 floorOffset, float2 uvOffset, out float3 position, out float currentT) {
    const float2 mipPos = mipRes * origin.xy;
    float2 plane = floor(mipPos) + floorOffset;
    plane = plane * mipInv + uvOffset;
    const float2 t = plane * invDirection.xy - origin.xy * invDirection.xy;
    currentT = min(t.x, t.y);
    position = origin + currentT * direction;
}

bool sssrAdvance(float3 origin, float3 direction, float3 invDirection, float2 mipPos, float2 mipInv, float2 floorOffset, float2 uvOffset, float surfaceZ, inout float3 position, inout float currentT) {
    float2 plane = floor(mipPos) + floorOffset;
    plane = plane * mipInv + uvOffset;
    const float3 boundary = float3(plane, surfaceZ);
    float3 t = boundary * invDirection - origin * invDirection;
    t.z = direction.z < 0.0 ? t.z : kSssrFloatMax;
    const float tMin = min(min(t.x, t.y), t.z);
    const bool above = surfaceZ < position.z;
    const bool skipped = asuint(tMin) != asuint(t.z) && above;
    currentT = above ? tMin : currentT;
    position = origin + currentT * direction;
    return skipped;
}

float2 sssrMipRes(int mip) {
    return float2(max(gFrame.extent.xy >> uint(clamp(mip, 0, kSssrMaxMip)), uint2(1, 1)));
}

float3 sssrMarch(float3 origin, float3 direction, int mostDetailed, out bool validHit) {
    const float3 invDirection = sssrSafeRcp(direction);
    int currentMip = mostDetailed;
    float2 mipRes = sssrMipRes(currentMip);
    float2 mipInv = 1.0 / mipRes;
    const float2 screen = float2(gFrame.extent.xy);
    float2 uvOffset = 0.005 * exp2(float(mostDetailed)) / screen;
    uvOffset.x = direction.x < 0.0 ? -uvOffset.x : uvOffset.x;
    uvOffset.y = direction.y < 0.0 ? -uvOffset.y : uvOffset.y;
    const float2 floorOffset = float2(direction.x < 0.0 ? 0.0 : 1.0, direction.y < 0.0 ? 0.0 : 1.0);
    float currentT;
    float3 position;
    sssrInitialAdvance(origin, direction, invDirection, mipRes, mipInv, floorOffset, uvOffset, position, currentT);
    const int stepCap = int(clamp(min(gSsr.steps, 32.0), 8.0, 32.0));
    int i = 0;
    validHit = false;
    while (i < stepCap && currentMip >= mostDetailed) {
        if (position.x < 0.0 || position.y < 0.0 || position.x > 1.0 || position.y > 1.0) {
            break;
        }
        const float2 mipPos = mipRes * position.xy;
        const float surfaceZ = sssrLoadDepth(mipPos, currentMip);
        bool skipped = sssrAdvance(origin, direction, invDirection, mipPos, mipInv, floorOffset, uvOffset, surfaceZ, position, currentT);
        if (gFrame.disableHiZ != 0u) {
            currentMip = 0;
        } else {
            int next = currentMip + (skipped ? 1 : -1);
            if (next > kSssrMaxMip) {
                next = kSssrMaxMip;
            }
            currentMip = next;
        }
        mipRes = sssrMipRes(currentMip);
        mipInv = 1.0 / mipRes;
        i += 1;
    }
    validHit = gFrame.disableHiZ != 0u ? i > 0 : currentMip < mostDetailed;
    return position;
}

bool sssrTakesRay(float rough) {
    return rough <= 0.65;
}

float sssrValidate(float3 hit, float2 uv, float3 worldDir, float2 screen) {
    if (hit.x < 0.0 || hit.y < 0.0 || hit.x > 1.0 || hit.y > 1.0) {
        return 0.0;
    }
    const float2 manhattan = abs(hit.xy - uv);
    if (manhattan.x < (2.0 / screen.x) && manhattan.y < (2.0 / screen.y)) {
        return 0.0;
    }
    const int2 texel = clamp(int2(screen * hit.xy), int2(0, 0), int2(screen) - int2(1, 1));
    const float surfaceZ = gDepth.Load(int3(texel, 0));
    if (surfaceZ <= 0.0) {
        return 0.0;
    }
    const float3 hitNormal = sssrLoadNormal(texel);
    if (dot(hitNormal, worldDir) > 0.0) {
        return 0.0;
    }
    const float3 surfaceP = reconstructWorld(hit.xy, surfaceZ);
    const float3 hitP = reconstructWorld(hit.xy, hit.z);
    const float distance = length(surfaceP - hitP);
    const float camDist = max(length(hitP - gFrame.cameraPos.xyz), 0.05);
    const float thick = max(max(gSsr.thick, kSssrThickness), camDist * 0.02);
    const float2 fov = 0.05 * float2(screen.y / max(screen.x, 1.0), 1.0);
    const float2 border = smoothstep(0.0, fov, hit.xy) * (1.0 - smoothstep(1.0 - fov, 1.0, hit.xy));
    float confidence = 1.0 - smoothstep(0.0, thick, distance);
    confidence *= confidence;
    return border.x * border.y * confidence;
}

float3 sssrSky(float3 dir) {
    float3 sky = max(gSkyView.SampleLevel(gClamp, atmSkyUv(dir), 0.0).rgb, 0.0);
    if ((gPass.passMask & 256u) == 0u) {
        sky *= 0.03;
    }
    return sky;
}

float3 sssrRt(float3 P, float3 N, float3 worldDir, float2 uv, uint2 px, out float travel) {
    travel = 0.0;
    if (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u) {
        return 0.0;
    }
    float hitT = 0.0;
    uint prim = 0u;
    float2 bary = 0.0;
    const float3 origin = P + N * 0.02 + worldDir * 0.08;
    const float tMax = max(gSsr.maxDist, 4.0);
    uint ignored;
    InterlockedAdd(gRayStat[1024u * 128u + 10u].x, 1u, ignored);
    if (!sharcTrace(origin, worldDir, 0.05, tMax, hitT, prim, bary)) {
        travel = tMax;
        return sssrSky(worldDir);
    }
    travel = hitT;
    const float3 hitP = origin + worldDir * hitT;
    const float2 screen = float2(gFrame.extent.xy);
    const float4 clip = worldToClip(hitP);
    if (clip.w > 1e-3) {
        const float2 huv = clip.xy / clip.w * 0.5 + 0.5;
        const float2 man = abs(huv - uv);
        if (huv.x >= 0.0 && huv.y >= 0.0 && huv.x <= 1.0 && huv.y <= 1.0
            && (man.x >= 2.0 / screen.x || man.y >= 2.0 / screen.y)) {
            const int2 hp = clamp(int2(screen * huv), int2(0, 0), int2(screen) - int2(1, 1));
            const float sceneZ = gDepth.Load(int3(hp, 0));
            if (sceneZ > 0.0) {
                const float3 sceneP = reconstructWorld(huv, sceneZ);
                const float gap = length(sceneP - hitP);
                if (gap < max(0.35, hitT * 0.04)) {
                    return max(gHdr[hp].rgb, 0.0);
                }
            }
        }
    }
    const SharcSurf surf = sharcSurface(prim, bary, hitP, -worldDir);
    if (!surf.hit) {
        return sssrSky(worldDir);
    }
    uint rng = px.x * 1973u ^ px.y * 9277u ^ gSharc.frameIndex;
    const float3 direct = sharcDirect(surf, -worldDir, rng);
    SharcHitData sharcHit;
    sharcHit.positionWorld = hitP;
    sharcHit.normalWorld = surf.geoNormal;
    sharcHit.materialDemodulation = surf.demod;
    float3 indirect = 0.0;
    sharcGetCachedRadiance(sharcHit, sharcGrid(), indirect);
    return max(direct + max(indirect, 0.0), 0.0);
}

groupshared uint tileGloss;

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectTrace(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex) {
    if (gtid == 0u) {
        tileGloss = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    const uint2 px = dtid.xy;
    const bool inside = px.x < gFrame.extent.x && px.y < gFrame.extent.y;
    const int2 ip = int2(px);
    const bool litView = gFrame.debugMode == 0u || gFrame.debugMode == 7u;
    if (inside && litView) {
        const uint vis = gVis.Load(int3(ip, 0));
        const float depth = gDepth.Load(int3(ip, 0));
        const float rough = gSurf0[ip].w;
        if (vis != 0u && depth > 0.0 && sssrTakesRay(rough)) {
            uint ignored;
            InterlockedOr(tileGloss, 1u, ignored);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (!inside || tileGloss == 0u || !litView) {
        if (inside) {
            gReflect[ip] = 0.0;
        }
        return;
    }
    const uint vis = gVis.Load(int3(ip, 0));
    const float depth = gDepth.Load(int3(ip, 0));
    const float rough = gSurf0[ip].w;
    if (vis == 0u || depth <= 0.0 || !sssrTakesRay(rough)) {
        gReflect[ip] = 0.0;
        return;
    }
    float3 N = octDecode(gSurf1[ip].xy);
    const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
    const float3 P = reconstructWorld(uv, depth);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    }
    N = normalize(N);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    float3 worldDir = sssrReflect(V, N, rough, ip);
    if (dot(worldDir, N) <= 0.0 || dot(worldDir, worldDir) < 1e-8) {
        worldDir = reflect(-V, N);
    }
    if (dot(worldDir, worldDir) < 1e-8) {
        gReflect[ip] = 0.0;
        return;
    }
    float3 origin = float3(uv, min(depth + 1.0e-4, 0.999999));
    const float3 screenDir = sssrProject(P + worldDir) - float3(uv, depth);
    if (dot(screenDir, screenDir) < 1e-12) {
        gReflect[ip] = 0.0;
        return;
    }
    bool valid = false;
    const float3 hit = sssrMarch(origin, screenDir, 0, valid);
    const float2 screen = float2(gFrame.extent.xy);
    float confidence = valid ? sssrValidate(hit, uv, worldDir, screen) : 0.0;
    const float3 hitP = reconstructWorld(saturate(hit.xy), hit.z);
    float rayLength = length(hitP - P);
    if (rayLength > max(gSsr.maxDist, 4.0)) {
        confidence = 0.0;
    }
    float3 radiance = 0.0;
    if (confidence > 0.0) {
        const int2 hitPx = clamp(int2(screen * hit.xy), int2(0, 0), int2(screen) - int2(1, 1));
        const float hitDepth = gDepth.Load(int3(hitPx, 0));
        const float3 hitWorld = reconstructWorld(hit.xy, hitDepth);
        const float3 hitN = sssrLoadNormal(hitPx);
        const bool sameFace = abs(dot(hitWorld - P, N)) < 0.12 && dot(hitN, N) > 0.85;
        if (sameFace) {
            confidence = 0.0;
        } else {
            radiance = max(gHdr[hitPx].rgb, 0.0) * confidence;
        }
    }
    if (confidence <= 0.0 && rough < 0.25) {
        float travel = 0.0;
        radiance = sssrRt(P, N, worldDir, uv, px, travel);
        rayLength = travel;
        confidence = rayLength > 0.0 ? 1.0 : 0.0;
    } else if (confidence <= 0.0) {
        radiance = sssrSky(worldDir);
        rayLength = max(gSsr.maxDist, 4.0);
        confidence = 1.0;
    }
    gReflect[ip] = float4(radiance, confidence > 0.0 ? rayLength : 0.0);
}

float sssrLuma(float3 color) {
    return max(dot(color, float3(0.299, 0.587, 0.114)), 0.001);
}

float sssrNormalWeight(float3 a, float3 b) {
    return pow(max(dot(a, b), 0.0), 512.0);
}

float sssrDepthWeight(float centerDepth, float neighborDepth) {
    return exp(-abs(centerDepth - neighborDepth) * centerDepth * 4.0);
}

float sssrRadianceWeight(float3 centerRadiance, float3 neighborRadiance, float variance) {
    const float bias = 0.6 + variance * 0.1;
    return max(exp(-bias * length(centerRadiance - neighborRadiance)), 1.0e-2);
}

float sssrKernel(int i) {
    const float radius = 5.0;
    const float x = float(i);
    return exp(-3.0 * (x * x) / (radius * radius));
}

float3 sssrClipAabb(float3 aabbMin, float3 aabbMax, float3 prevSample) {
    const float3 center = 0.5 * (aabbMax + aabbMin);
    const float3 extent = 0.5 * (aabbMax - aabbMin) + 0.001;
    const float3 delta = prevSample - center;
    const float3 unit = abs(delta / extent);
    const float maxAbs = max(max(unit.x, unit.y), unit.z);
    if (maxAbs > 1.0) {
        return center + delta / maxAbs;
    }
    return prevSample;
}

float4 sssrLoadHist(int2 p) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), ext);
    const float4 h = gPush.flight == 0u ? gHist1[c] : gHist0[c];
    if (!(h.a >= 1.0) || h.a > 48.0 || any(isnan(h.rgb)) || any(isinf(h.rgb))) {
        return 0.0;
    }
    return h;
}

void sssrStoreHist(int2 p, float4 v) {
    if (gPush.flight == 0u) {
        gHist0[p] = v;
    } else {
        gHist1[p] = v;
    }
}

float4x4 sssrPrevVp() {
    return float4x4(
        float4(gBlock.prev0.x, gBlock.prev1.x, gBlock.prev2.x, gBlock.prev3.x),
        float4(gBlock.prev0.y, gBlock.prev1.y, gBlock.prev2.y, gBlock.prev3.y),
        float4(gBlock.prev0.z, gBlock.prev1.z, gBlock.prev2.z, gBlock.prev3.z),
        float4(gBlock.prev0.w, gBlock.prev1.w, gBlock.prev2.w, gBlock.prev3.w));
}

float2 sssrPrevUv(float3 world) {
    const float4 prevClip = mul(sssrPrevVp(), float4(world, 1.0));
    return prevClip.xy / max(prevClip.w, 1e-5) * 0.5 + 0.5;
}

bool sssrInside(float2 uv) {
    return uv.x > 0.0 && uv.y > 0.0 && uv.x < 1.0 && uv.y < 1.0;
}

float sssrDisocclude(float3 n, float3 hn, float depth, float histDepth) {
    const float nd = max(depth, 1e-3);
    return exp(-abs(1.0 - max(dot(n, hn), 0.0)) * 1.4) * exp(-abs(histDepth - nd) / nd);
}

int2 sssrUvPx(float2 uv, float2 screen) {
    return clamp(int2(uv * screen), int2(0, 0), int2(screen) - int2(1, 1));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectUp(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return;
    }
    const int2 ip = int2(px);
    if (gPush.pad0 == 2u) {
        gHist0[ip] = 0.0;
        gHist1[ip] = 0.0;
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 7u) {
        return;
    }
    const float4 raw = gReflect[ip];
    const float4 surf = gSurf0[ip];
    const float4 surfN = gSurf1[ip];
    const float rough = surf.w;
    const float metal = saturate(surfN.z);
    if (!sssrTakesRay(rough)) {
        sssrStoreHist(ip, 0.0);
        return;
    }
    const float2 screen = float2(gFrame.extent.xy);
    const float blur = max(gSsr.stride, 0.25) * lerp(1.0, 3.0, saturate((rough - 0.25) / 0.40));
    const float variance = rough <= kSssrMirror ? 0.0 : rough * rough;
    float3 resolved = raw.rgb;
    float3 avg = raw.rgb;
    if (raw.a > 0.0 && variance > 0.0) {
        const float3 n0 = sssrLoadNormal(ip);
        const float d0 = gDepth.Load(int3(ip, 0));
        const float varianceWeight = max(0.1, 1.0 - exp(-(variance * 4.4)));
        float wSum = sssrRadianceWeight(raw.rgb, raw.rgb, variance);
        float3 acc = raw.rgb * wSum;
        float avgW = 1.0;
        float3 avgAcc = raw.rgb;
        for (int i = 0; i < 15; ++i) {
            const int2 delta = int2(round(float2(kSssrPrefilter[i]) * blur));
            const int2 tap = clamp(ip + delta, int2(0, 0), int2(screen) - int2(1, 1));
            const float4 nb = gReflect[tap];
            if (nb.a <= 0.0) {
                continue;
            }
            const float spatial = sssrKernel(delta.x) * sssrKernel(delta.y);
            float w = sssrNormalWeight(n0, sssrLoadNormal(tap));
            w *= sssrDepthWeight(d0, gDepth.Load(int3(tap, 0)));
            w *= sssrRadianceWeight(raw.rgb, nb.rgb, variance);
            w *= varianceWeight * spatial;
            wSum += w;
            acc += nb.rgb * w;
            avgW += 1.0;
            avgAcc += nb.rgb;
        }
        resolved = acc / max(wSum, 1e-4);
        avg = avgAcc / max(avgW, 1e-4);
    }
    float numSamples = 1.0;
    if (raw.a > 0.0 && rough > kSssrMirror) {
        const float depth = gDepth.Load(int3(ip, 0));
        const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
        const float3 P = reconstructWorld(uv, depth);
        float3 N = octDecode(surfN.xy);
        if (dot(N, N) < 0.5) {
            N = reconstructNormalFromDepth(gDepth, px, P);
        }
        N = normalize(N);
        const float3 V = normalize(gFrame.cameraPos.xyz - P);
        const float3 mirrorDir = reflect(-V, N);
        const float2 surfaceUv = sssrPrevUv(P);
        const float2 hitUv = sssrPrevUv(P + mirrorDir * raw.a);
        const float linearDepth = max(length(P - gFrame.cameraPos.xyz), 1e-3);
        float3 mean = 0.0;
        float3 moment = 0.0;
        float weightSum = 0.0;
        for (int y = -4; y <= 4; ++y) {
            for (int x = -4; x <= 4; ++x) {
                const int2 tap = clamp(ip + int2(x, y), int2(0, 0), int2(screen) - int2(1, 1));
                const float4 nb = gReflect[tap];
                if (nb.a <= 0.0) {
                    continue;
                }
                const float w = sssrKernel(x) * sssrKernel(y);
                weightSum += w;
                mean += nb.rgb * w;
                moment += nb.rgb * nb.rgb * w;
            }
        }
        if (weightSum > 0.0) {
            mean /= weightSum;
            moment /= weightSum;
        } else {
            mean = resolved;
        }
        const float3 localVar = abs(moment - mean * mean);
        float2 historyUv = surfaceUv;
        float4 history = 0.0;
        float factor = 0.0;
        bool picked = false;
        const bool surfaceOk = sssrInside(surfaceUv);
        const bool hitOk = sssrInside(hitUv);
        if (surfaceOk || hitOk) {
            const int2 surfacePx = sssrUvPx(surfaceUv, screen);
            const int2 hitPx = sssrUvPx(hitUv, screen);
            const float3 surfaceN = sssrLoadNormal(surfacePx);
            const float3 hitN = sssrLoadNormal(hitPx);
            const float surfaceSim = dot(surfaceN, N);
            const float hitSim = dot(hitN, N);
            const float surfaceRough = gSurf0[surfacePx].w;
            const float hitRough = gSurf0[hitPx].w;
            const float4 surfaceHist = surfaceOk ? sssrLoadHist(surfacePx) : 0.0;
            const float4 hitHist = hitOk ? sssrLoadHist(hitPx) : 0.0;
            const bool hitMatch = hitOk && hitSim > 0.9999 && hitSim + 1.0e-3 > surfaceSim && abs(hitRough - rough) < abs(surfaceRough - rough) + 1.0e-3;
            if (hitMatch) {
                historyUv = hitUv;
                history = hitHist;
                picked = true;
            } else if (surfaceOk) {
                const float3 delta = surfaceHist.rgb - mean;
                if (dot(delta, delta) < 1.5 * length(localVar)) {
                    historyUv = surfaceUv;
                    history = surfaceHist;
                    picked = true;
                }
            }
            if (picked && history.a >= 1.0) {
                const int2 hp = sssrUvPx(historyUv, screen);
                const float3 hn = sssrLoadNormal(hp);
                const float hd = max(length(reconstructWorld(historyUv, gDepth.Load(int3(hp, 0))) - gFrame.cameraPos.xyz), 1e-3);
                factor = sssrDisocclude(N, hn, linearDepth, hd);
                if (factor < 0.9) {
                    float best = factor;
                    float2 bestUv = historyUv;
                    for (int sy = -1; sy <= 1; ++sy) {
                        for (int sx = -1; sx <= 1; ++sx) {
                            const float2 u = historyUv + float2(sx, sy) / screen;
                            if (!sssrInside(u)) {
                                continue;
                            }
                            const int2 sp = sssrUvPx(u, screen);
                            const float sd = max(length(reconstructWorld(u, gDepth.Load(int3(sp, 0))) - gFrame.cameraPos.xyz), 1e-3);
                            const float w = sssrDisocclude(N, sssrLoadNormal(sp), linearDepth, sd);
                            if (w > best) {
                                best = w;
                                bestUv = u;
                            }
                        }
                    }
                    historyUv = bestUv;
                    history = sssrLoadHist(sssrUvPx(historyUv, screen));
                    factor = best;
                }
            }
        }
        if (factor >= 0.9 && history.a >= 1.0) {
            const float cap = max(8.0, 32.0 * (1.0 - exp(-rough * 100.0)));
            numSamples = min(cap, history.a * factor + 1.0);
            const float3 colorStd = (sqrt(localVar) + length(mean - avg)) * 1.4;
            const float3 boxMean = lerp(mean, avg, 0.2);
            const float3 clipped = sssrClipAabb(boxMean - colorStd, boxMean + colorStd, history.rgb);
            const float speed = 1.0 / max(numSamples, 1.0);
            float3 signal = lerp(resolved, avg, 1.0 / max(numSamples + 1.0, 1.0));
            signal = sssrClipAabb(avg - colorStd, avg + colorStd, signal);
            resolved = lerp(signal, clipped, 1.0 - speed);
            if (any(isnan(resolved)) || any(isinf(resolved))) {
                resolved = raw.rgb;
                numSamples = 1.0;
            }
        }
    } else {
        resolved = 0.0;
    }
    sssrStoreHist(ip, float4(max(resolved, 0.0), numSamples));
    if (gFrame.debugMode == 7u) {
        gHdr[ip] = float4(max(resolved, 0.0), 1.0);
        return;
    }
    const float depth = gDepth.Load(int3(ip, 0));
    if (depth <= 0.0 || raw.a <= 0.0) {
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    float3 N = octDecode(surfN.xy);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    }
    N = normalize(N);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    const float ndv = saturate(dot(N, V));
    const float3 f0 = computeF0(surf.rgb, metal, 0.04);
    const float2 dfg = dfgEnv(ndv, rough);
    const float3 spec = max(resolved, 0.0) * (f0 * dfg.x + dfg.y);
    const float3 color = gHdr[ip].rgb + spec;
    gHdr[ip] = float4(max(color, 0.0), 1.0);
}
