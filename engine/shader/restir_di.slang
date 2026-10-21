#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8

#include "restir_rab.slang"
#include "Rtxdi/LightSampling/RISBuffer.hlsli"
#include "Rtxdi/DI/InitialSampling.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"
#include "Rtxdi/DI/BoilingFilter.hlsli"

[[vk::binding(26, 0)]] RWTexture2D<float4> gSigmaPenumbra;
[[vk::binding(27, 0)]] RWTexture2D<float4> gSigmaSun;

static const uint kRestirInitialSeed = 1u;
static const uint kRestirTemporalSeed = 2u;
static const uint kRestirSpatialSeed = 3u;

bool restirInside(uint2 px) {
    return px.x < gFrame.extent.x && px.y < gFrame.extent.y;
}

void restirInitial(uint2 px) {
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(px, gRestir.runtime.frameIndex, kRestirInitialSeed);
    RTXDI_RandomSamplerState tileRng = RTXDI_InitRandomSampler(px / RTXDI_TILE_SIZE_IN_PIXELS, gRestir.runtime.frameIndex, kRestirInitialSeed);
    RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
    RAB_LightSample lightSample;
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    if (RAB_IsSurfaceValid(surface) && gRestir.lightCount > 0u) {
        reservoir = RTXDI_SampleLightsForSurface(
            rng, tileRng, surface, gRestir.initial, gRestir.lights, gRestir.localRis, gRestir.envRis, lightSample);
    }
    RTXDI_StoreDIReservoir(reservoir, gRestir.reservoir, px, restirBuf(gRestir.indices.initialSamplingOutputBufferIndex));
}

void restirTemporal(uint2 px, uint2 local, bool inside) {
    RTXDI_DIReservoir temporal = RTXDI_EmptyDIReservoir();
    if (inside) {
        RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(px, gRestir.runtime.frameIndex, kRestirTemporalSeed);
        RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
        if (RAB_IsSurfaceValid(surface)) {
            RTXDI_DIReservoir current = RTXDI_LoadDIReservoir(
                gRestir.reservoir, px, restirBuf(gRestir.indices.initialSamplingOutputBufferIndex));
            int2 temporalPixel = int2(-1, -1);
            RAB_LightSample selected = (RAB_LightSample)0;
            temporal = RTXDI_DITemporalResampling(
                px, surface, current, rng, gRestir.runtime, gRestir.reservoir, restirMotion(px, surface),
                restirBuf(gRestir.indices.temporalResamplingInputBufferIndex), gRestir.temporal, temporalPixel, selected);
        }
    }
    if (gRestir.boiling.enableBoilingFilter != 0u) {
        RTXDI_BoilingFilter(local, gRestir.boiling.boilingFilterStrength, temporal);
    }
    if (inside) {
        RTXDI_StoreDIReservoir(temporal, gRestir.reservoir, px, restirBuf(gRestir.indices.temporalResamplingOutputBufferIndex));
    }
}

void restirSpatial(uint2 px) {
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(px, gRestir.runtime.frameIndex, kRestirSpatialSeed);
    RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
    RTXDI_DIReservoir spatial = RTXDI_EmptyDIReservoir();
    if (RAB_IsSurfaceValid(surface)) {
        RTXDI_DIReservoir center = RTXDI_LoadDIReservoir(
            gRestir.reservoir, px, restirBuf(gRestir.indices.spatialResamplingInputBufferIndex));
        RAB_LightSample selected = (RAB_LightSample)0;
        spatial = RTXDI_DISpatialResampling(
            px, surface, center, rng, gRestir.runtime, gRestir.reservoir,
            restirBuf(gRestir.indices.spatialResamplingInputBufferIndex), gRestir.spatial, selected);
    }
    RTXDI_StoreDIReservoir(spatial, gRestir.reservoir, px, restirBuf(gRestir.indices.spatialResamplingOutputBufferIndex));
}

void restirStat(uint slot, uint n) {
    uint ignored;
    uint index = gRestir.localRis.tileSize * gRestir.localRis.tileCount + slot;
    InterlockedAdd(gRis[index].x, n, ignored);
}

float3 restirIdColor(uint lightIndex) {
    const float3 palette[8] = {
        float3(1.0, 0.82, 0.15),
        float3(0.25, 0.55, 1.0),
        float3(1.0, 0.28, 0.18),
        float3(0.2, 0.9, 0.35),
        float3(0.85, 0.3, 0.95),
        float3(0.2, 0.9, 0.9),
        float3(1.0, 0.5, 0.1),
        float3(0.95, 0.95, 0.95),
    };
    return palette[lightIndex & 7u];
}

float3 restirViz(uint mode, bool validRes, float3 radiance, float invPdf, float sampleM, float litAmt, float visible, uint lightIndex, float3 shadowTint) {
    if (mode == 18u) {
        return validRes ? shadowTint : 1.0;
    }
    float3 miss = float3(0.02, 0.06, 0.22);
    if (!validRes) {
        return miss;
    }
    if (mode == 15u) {
        float3 col = radiance * litAmt;
        float peak = max(col.r, max(col.g, col.b));
        return col / (1.0 + peak);
    }
    if (mode == 16u) {
        float w = saturate(invPdf * litAmt / 0.45);
        return lerp(float3(0.04, 0.06, 0.16), float3(1.0, 0.72, 0.12), w);
    }
    if (mode == 17u) {
        float t = saturate(sampleM / 20.0);
        return lerp(float3(0.12, 0.22, 1.0), float3(1.0, 0.92, 0.25), t);
    }
    return restirIdColor(lightIndex);
}

void restirShadePixel(uint2 px) {
    float depth = gDepth.Load(int3(px, 0));
    float4 s0 = gSurf0[px];
    float4 s1 = gSurf1[px];
    RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
    uint mode = gFrame.debugMode;
    bool debugView = mode >= 15u && mode <= 19u;
    bool lit = mode == 0u || mode == 4u || mode == 8u || debugView;
    float3 add = 0.0;
    bool validRes = false;
    float visible = 0.0;
    float3 shadowTint = 1.0;
    float invPdf = 0.0;
    float sampleM = 0.0;
    float litAmt = 0.0;
    uint lightIndex = 0u;
    float3 radiance = 0.0;
    bool surfaceOn = RAB_IsSurfaceValid(surface) && depth > 0.0001;
    if (surfaceOn) {
        restirStat(0u, 1u);
        if (lit) {
            RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(
                gRestir.reservoir, px, restirBuf(gRestir.indices.shadingInputBufferIndex));
            validRes = RTXDI_IsValidDIReservoir(reservoir);
            if (validRes) {
                lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
                RAB_LightInfo info = RAB_LoadLightInfo(lightIndex, false);
                RAB_LightSample sample = RAB_SamplePolymorphicLight(info, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
                uint rays = max(gRestir.shadowRays, 1u);
                float3 tintSum = 0.0;
                float3 toL = sample.position - surface.worldPos;
                float dist = length(toL);
                float3 toCenter = info.posRadius.xyz - surface.worldPos;
                float centerDist = length(toCenter);
                float3 dir = centerDist > 1e-4 ? toCenter / centerDist : float3(0.0, 1.0, 0.0);
                float source = restirSourceRadius(info);
                float tMin = max(gRestir.shadowBias, 0.02);
                float3 origin = surface.worldPos + surface.geoNormal * 0.02;
                for (uint ray = 0u; ray < rays; ++ray) {
                    float3 castPos = sample.position;
                    if (ray > 0u) {
                        float u = (float(ray) + 0.5) / float(rays);
                        float v = frac(0.5 + float(ray) * 0.754877666);
                        castPos = restirLightDisk(info.posRadius.xyz, dir, source, u, v);
                    }
                    float3 toCast = castPos - surface.worldPos;
                    float castDist = length(toCast);
                    float reach = max(castDist - tMin, tMin);
                    if (gRestir.shadowRange > 0.0) {
                        reach = min(reach, gRestir.shadowRange);
                    }
                    float3 castDir = castDist > 1e-4 ? toCast / castDist : dir;
                    tintSum += restirAnyHit(origin, castDir, tMin, reach);
                }
                float3 tint = tintSum / float(rays);
                shadowTint = tint;
                visible = restirLuma(tint);
                invPdf = max(RTXDI_GetDIReservoirInvPdf(reservoir), 0.0);
                sampleM = reservoir.M;
                radiance = sample.radiance;
                add = restirShade(sample, surface) * invPdf * visible;
                float facing = dist > 1e-4 ? saturate(dot(surface.normal, toL / dist)) : 0.0;
                litAmt = visible * facing;
                restirStat(1u, 1u);
                restirStat(visible > 0.5 ? 2u : 3u, 1u);
                restirStat(4u, uint(min(max(sampleM, 0.0), 4096.0)));
                restirStat(5u, uint(min(invPdf, 64.0) * 1000.0));
                restirStat(6u, 1u);
            }
        }
    }
    if (debugView) {
        float3 viz = surfaceOn ? restirViz(mode, validRes, radiance, invPdf, sampleM, litAmt, visible, lightIndex, shadowTint) : 0.0;
        gHdr[px] = float4(viz, 1.0);
    } else if (mode == 4u && validRes) {
        gHdr[px] = float4(max(add, 0.0), 1.0);
    } else if (lit) {
        float4 hdr = gHdr[px];
        hdr.rgb += max(add, 0.0);
        gHdr[px] = hdr;
    }
    restirWriteHistory(px, surface, s0.rgb, s0.w, s1.z);
}

float3 restirSunDir(uint2 px, float3 L, uint ray) {
    float halfA = radians(max(gRestir.sunDisk, 0.0)) * 0.5;
    if (halfA < 1e-5) {
        return L;
    }
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(px, gRestir.runtime.frameIndex, 4u + ray);
    float u = RTXDI_GetNextRandom(rng);
    float v = RTXDI_GetNextRandom(rng);
    float radius = sqrt(u) * halfA;
    float phi = v * 6.2831853;
    float3 up = abs(L.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, L));
    float3 bitangent = cross(L, tangent);
    return normalize(L + tangent * (radius * cos(phi)) + bitangent * (radius * sin(phi)));
}

void restirSigmaStore(uint2 px, float vis, float penumbra, float3 sunRgb, float valid) {
    const uint2 at = uint2(px.x, px.y + gPush.pad0 * gFrame.extent.y);
    gSigmaPenumbra[at] = float4(saturate(vis), penumbra, 0.0, valid);
    gSigmaSun[at] = float4(max(sunRgb, 0.0), valid);
}

void restirSun(uint2 px) {
    float depth = gDepth.Load(int3(px, 0));
    RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
    if (depth <= 0.0001 || !RAB_IsSurfaceValid(surface)) {
        restirSigmaStore(px, 0.0, 0.0, 0.0, 0.0);
        return;
    }
    float3 L = gSun.sunDirIntensity.xyz;
    float len = length(L);
    if (len < 1e-4) {
        restirSigmaStore(px, 0.0, 0.0, 0.0, 0.0);
        return;
    }
    L /= len;
    float3 origin = surface.worldPos + surface.geoNormal * 0.02;
    uint rays = max(gRestir.pad2, 1u);
    float3 tint = 0.0;
    float closest = -1.0;
    for (uint ray = 0u; ray < rays; ++ray) {
        float3 dir = restirSunDir(px, L, ray);
        float sunFar = gRestir.shadowRange > 0.0 ? gRestir.shadowRange : 20000.0;
        RestirOcclusion occ = restirSunOcclusion(origin, dir, max(gRestir.shadowBias, 0.02), sunFar);
        tint += occ.tint;
        if (occ.distance >= 0.0) {
            closest = closest < 0.0 ? occ.distance : min(closest, occ.distance);
        }
    }
    tint /= float(rays);
    float vis = restirLuma(tint);
    const float rayVis = vis;
    const float rayClosest = closest;
    uint age = 1u;
    bool temporalOn = (gRestir.pad1 & 1u) != 0u;
    if (temporalOn && gRestir.historyValid != 0u) {
        float3 motion = restirMotion(px, surface);
        int2 prev = int2(round(float2(px) + motion.xy));
        if (all(prev >= int2(0, 0)) && all(prev < int2(gFrame.extent))) {
            uint2 at = restirSliceOrigin(gRestir.readSlice) + uint2(prev);
            float4 pos = gHistPos[at];
            uint4 mat = gHistMat[at];
            float prevDepth = pos.w;
            float depthSpan = gRestir.temporal.depthThreshold * max(surface.viewDepth, 1e-3);
            bool depthOk = prevDepth > 0.0 && abs(prevDepth - surface.viewDepth) <= depthSpan;
            bool normalOk = dot(restirUnpackOct(mat.x), surface.normal) >= gRestir.temporal.normalThreshold;
            if (depthOk && normalOk) {
                float prevVis = float(mat.w & 0xffffu) / 65535.0;
                uint prevAge = mat.w >> 16;
                age = min(prevAge + 1u, max(gRestir.temporal.maxHistoryLength, 1u));
                float blended = lerp(prevVis, vis, 1.0 / float(age));
                tint *= blended / max(vis, 1e-3);
                vis = blended;
            }
        }
    }
    uint2 writeAt = restirSliceOrigin(gRestir.writeSlice) + px;
    uint4 stored = gHistMat[writeAt];
    stored.w = min(uint(saturate(vis) * 65535.0 + 0.5), 65535u) | (age << 16);
    gHistMat[writeAt] = stored;
    float ndl = dot(surface.geoNormal, L);
    float halfA = radians(max(gRestir.sunDisk, 0.0)) * 0.5;
    float penumbra = 4096.0;
    float sigmaVis = vis;
    if (ndl <= 0.0) {
        sigmaVis = 0.0;
        penumbra = 0.0;
    } else if (rayClosest >= 0.0) {
        penumbra = rayClosest * tan(halfA);
    }
    RAB_LightSample sample = RAB_EmptyLightSample();
    sample.position = surface.worldPos + L * 20000.0;
    sample.normal = -L;
    sample.radiance = max(gSun.sunColor.rgb, 0.0) * max(gSun.sunDirIntensity.w, 0.0);
    sample.solidAnglePdf = 1.0;
    float3 shade = max(restirShade(sample, surface), 0.0);
    restirSigmaStore(px, sigmaVis, penumbra, shade, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID) {
    uint2 px = dtid.xy;
    bool inside = restirInside(px);
    if (gPush.pass == 1u) {
        restirTemporal(px, gtid.xy, inside);
        return;
    }
    if (!inside) {
        return;
    }
    if (gPush.pass == 0u) {
        restirInitial(px);
    } else if (gPush.pass == 2u) {
        restirSpatial(px);
    } else if (gPush.pass == 4u) {
        restirSun(px);
    } else {
        restirShadePixel(px);
    }
}
