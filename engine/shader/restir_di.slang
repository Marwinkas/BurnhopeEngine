#define RTXDI_ENABLE_BOILING_FILTER
#define RTXDI_BOILING_FILTER_GROUP_SIZE 8

#include "restir_rab.slang"
#include "core/atmosphere.slang"
#include "Rtxdi/LightSampling/RISBuffer.hlsli"
#include "Rtxdi/DI/InitialSampling.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"
#include "Rtxdi/DI/BoilingFilter.hlsli"

[[vk::binding(26, 0)]] RWTexture2D<float4> gSigmaPenumbra;
[[vk::binding(27, 0)]] RWTexture2D<float4> gSigmaSun;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;

static const uint kRestirInitialSeed = 1u;
static const uint kRestirTemporalSeed = 2u;
static const uint kRestirSpatialSeed = 3u;

bool restirInside(uint2 px) {
    return px.x < gFrame.extent.x && px.y < gFrame.extent.y;
}

float2 restirBlueNoise2(uint2 px, uint frame, uint salt) {
    uint n = frame * 11u + salt * 7u;
    float2 p = float2(px) + float2(float(n & 63u), float((n >> 6u) & 63u));
    float u = frac(52.9829189 * frac(0.06711056 * p.x + 0.00583715 * p.y) + float(n) * 0.754877666);
    float v = frac(52.9829189 * frac(0.00583715 * p.x - 0.06711056 * p.y) + float(n) * 0.569840291);
    return float2(u, v);
}

void restirInitial(uint2 px) {
    RTXDI_RandomSamplerState rng = RTXDI_InitRandomSampler(px, gRestir.runtime.frameIndex, kRestirInitialSeed);
    RTXDI_RandomSamplerState tileRng = RTXDI_InitRandomSampler(px / RTXDI_TILE_SIZE_IN_PIXELS, gRestir.runtime.frameIndex, kRestirInitialSeed);
    RAB_Surface surface = RAB_GetGBufferSurface(int2(px), false);
    RAB_LightSample lightSample;
    RTXDI_DIReservoir reservoir = RTXDI_EmptyDIReservoir();
    if (RAB_IsSurfaceValid(surface) && gRestir.lights.localLightBufferRegion.numLights > 0u) {
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

float3 restirEvalReservoir(uint2 px, RAB_Surface surface, RTXDI_DIReservoir reservoir, uint salt, out float outVis, out float3 outTint) {
    outVis = 0.0;
    outTint = 1.0;
    if (!RTXDI_IsValidDIReservoir(reservoir)) {
        return 0.0;
    }
    uint lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
    RAB_LightInfo info = RAB_LoadLightInfo(lightIndex, false);
    RAB_LightSample sample = RAB_SamplePolymorphicLight(info, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
    if (sample.solidAnglePdf <= 0.0) {
        return 0.0;
    }
    uint rays = max(gRestir.shadowRays, 1u);
    float3 tintSum = 0.0;
    float3 toCenter = info.posRadius.xyz - surface.worldPos;
    float centerDist = length(toCenter);
    if (centerDist <= 1e-4) {
        return 0.0;
    }
    float3 dir = toCenter / centerDist;
    float source = min(restirSourceRadius(info), centerDist * 0.25);
    float tMin = max(gRestir.shadowBias, 0.02);
    float3 origin = surface.worldPos + surface.geoNormal * 0.02 + dir * 0.01;
    for (uint ray = 0u; ray < rays; ++ray) {
        float2 xi = restirBlueNoise2(px, gRestir.runtime.frameIndex, salt * 8u + ray);
        float3 castPos = restirLightDisk(info.posRadius.xyz, dir, source, xi.x, xi.y);
        float3 toCast = castPos - surface.worldPos;
        float castDist = length(toCast);
        float reach = max(castDist - tMin * 2.0, tMin);
        if (gRestir.shadowRange > 0.0) {
            reach = min(reach, gRestir.shadowRange);
        }
        float3 castDir = castDist > 1e-4 ? toCast / castDist : dir;
        tintSum += restirAnyHit(origin, castDir, tMin, reach, (info.flags & 2u) != 0u);
    }
    outTint = tintSum / float(rays);
    outVis = restirLuma(outTint);
    float invPdf = clamp(RTXDI_GetDIReservoirInvPdf(reservoir), 0.0, 16.0);
    return restirShade(sample, surface) * invPdf * outTint;
}

bool restirRoom(RAB_Surface surface) {
    float3 n = surface.normal;
    if (dot(n, n) < 0.5) {
        n = float3(0.0, 1.0, 0.0);
    } else {
        n = normalize(n);
    }
    return restirTrace(surface.worldPos + n * 0.05, float3(0.0, 1.0, 0.0), 0.05, 80.0);
}

float3 restirSkyTerm(uint2 px, RAB_Surface surface) {
    if ((gPass.passMask & 1024u) == 0u) {
        return 0.0;
    }
    const bool early = (gPass.passMask & 2097152u) != 0u;
    const bool worldRc = (gPass.passMask & 262144u) != 0u && (gPass.passMask & 128u) != 0u;
    if (!early && worldRc) {
        return 0.0;
    }
    const float3 albedo = max(gSurf0[px].rgb, 0.0);
    const float metal = saturate(gSurf1[px].z);
    const float ao = saturate(gSurf1[px].w);
    const float wrap = saturate(surface.normal.y * 0.4 + 0.6);
    const float inten = gSun.sunDirIntensity.w;
    float3 tint = float3(0.78, 0.82, 0.88);
    if (!early) {
        const float3 n = surface.normal;
        const float3 skyDir = normalize(float3(n.x, n.y + 1.15, n.z));
        const float3 skyLit = max(gSkyView.SampleLevel(gClamp, atmSkyUv(skyDir), 0.0).rgb, 0.0);
        const float skyLum = dot(skyLit, float3(0.2126, 0.7152, 0.0722));
        const float3 chroma = skyLum > 0.05 ? skyLit / skyLum : tint;
        tint = lerp(tint, chroma, 0.35);
    }
    return albedo * (1.0 - metal) * tint * (inten * 0.055) * wrap * ao;
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
            const bool room = restirRoom(surface);
            if (room && (mode == 0u || mode == 4u)) {
                float4 hdr = gHdr[px];
                hdr.rgb = max(hdr.rgb - restirSkyTerm(px, surface), 0.0);
                gHdr[px] = hdr;
            }
            uint shadeBuf = restirBuf(gRestir.indices.shadingInputBufferIndex);
            RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(gRestir.reservoir, px, shadeBuf);
            validRes = RTXDI_IsValidDIReservoir(reservoir);
            if (validRes) {
                lightIndex = RTXDI_GetDIReservoirLightIndex(reservoir);
                RAB_LightInfo info = RAB_LoadLightInfo(lightIndex, false);
                RAB_LightSample sample = RAB_SamplePolymorphicLight(info, surface, RTXDI_GetDIReservoirSampleUV(reservoir));
                add = restirEvalReservoir(px, surface, reservoir, 0u, visible, shadowTint);
                invPdf = clamp(RTXDI_GetDIReservoirInvPdf(reservoir), 0.0, 16.0);
                sampleM = reservoir.M;
                radiance = sample.radiance;
                float3 toL = sample.position - surface.worldPos;
                float dist = length(toL);
                float facing = dist > 1e-4 ? saturate(dot(surface.normal, toL / dist)) : 0.0;
                litAmt = visible * facing;

                // ОБЯЗАТЕЛЬНО по стандарту NVIDIA RTXDI: записываем итоговую видимость обратно в резервуар,
                // чтобы перекрытые стенами источники света не переходили в следующий кадр через Temporal Resampling!
                reservoir.weightSum *= visible;
                if (visible <= 1e-3) {
                    reservoir = RTXDI_EmptyDIReservoir();
                }
                RTXDI_StoreDIReservoir(reservoir, gRestir.reservoir, px, shadeBuf);

                restirStat(1u, 1u);
                restirStat(visible > 0.5 ? 2u : 3u, 1u);
                restirStat(4u, uint(min(max(sampleM, 0.0), 4096.0)));
                restirStat(5u, uint(min(invPdf, 64.0) * 1000.0));
                restirStat(6u, 1u);
            }

            // Пространственное смешивание с 2 соседними резервуарами и демодулированное темпоральное накопление
            // в свободных срезах 2..3 текстуры gHistPos (полностью убирает цветной шум в закрытых комнатах)
            float3 demod = max(surface.material.diffuseAlbedo + surface.material.specularF0, float3(0.04, 0.04, 0.04));
            float3 curIrrad = add / demod;
            float wSum = 1.0;

            if (!room) {
                const int2 offsets[2] = { int2(1, 0), int2(0, 1) };
                [unroll]
                for (uint k = 0u; k < 2u; ++k) {
                    int2 q = int2(px) + offsets[k] * 2;
                    if (all(q >= int2(0, 0)) && all(q < int2(gFrame.extent))) {
                        RAB_Surface qSurf = RAB_GetGBufferSurface(q, false);
                        if (RAB_IsSurfaceValid(qSurf)) {
                            float dz = abs(qSurf.viewDepth - surface.viewDepth) / max(surface.viewDepth, 1e-3);
                            float nd = dot(qSurf.normal, surface.normal);
                            if (dz < 0.04 && nd > 0.92) {
                                RTXDI_DIReservoir qRes = RTXDI_LoadDIReservoir(gRestir.reservoir, uint2(q), shadeBuf);
                                if (RTXDI_IsValidDIReservoir(qRes) && RTXDI_GetDIReservoirLightIndex(qRes) != lightIndex) {
                                    float qVis = 0.0;
                                    float3 qTint = 1.0;
                                    float3 qAdd = restirEvalReservoir(px, surface, qRes, k + 1u, qVis, qTint);
                                    float w = 0.35 * pow(saturate(nd), 16.0);
                                    curIrrad += (qAdd / demod) * w;
                                    wSum += w;
                                }
                            }
                        }
                    }
                }
            }
            curIrrad /= wSum;

            // Подавление единичных ярких выбросов (firefly clamp)
            float curLuma = restirLuma(curIrrad);
            if (curLuma > 12.0) {
                curIrrad *= 12.0 / curLuma;
            }

            float diAge = 1.0;
            bool temporalOn = (gRestir.pad1 & 1u) != 0u;
            if (temporalOn && gRestir.historyValid != 0u) {
                float3 motion = restirMotion(px, surface);
                int2 prev = int2(round(float2(px) + motion.xy));
                if (all(prev >= int2(0, 0)) && all(prev < int2(gFrame.extent))) {
                    uint2 prevGeoAt = restirSliceOrigin(gRestir.readSlice) + uint2(prev);
                    float4 prevPos = gHistPos[prevGeoAt];
                    uint4 prevMat = gHistMat[prevGeoAt];
                    float prevDepth = prevPos.w;
                    float depthSpan = 0.06 * max(surface.viewDepth, 1e-3);
                    bool depthOk = prevDepth > 0.0 && abs(prevDepth - surface.viewDepth) <= depthSpan;
                    bool normalOk = dot(restirUnpackOct(prevMat.x), surface.normal) >= 0.9;
                    if (depthOk && normalOk) {
                        uint2 prevDiAt = restirSliceOrigin(gRestir.readSlice + 2u) + uint2(prev);
                        float4 prevDi = gHistPos[prevDiAt];
                        if (prevDi.w >= 1.0 && !any(isnan(prevDi.rgb)) && !any(isinf(prevDi.rgb))) {
                            diAge = min(prevDi.w + 1.0, room ? 64.0 : 12.0);
                            float alpha = 1.0 / diAge;
                            curIrrad = lerp(max(prevDi.rgb, 0.0), curIrrad, alpha);
                        }
                    }
                }
            }
            uint2 writeDiAt = restirSliceOrigin(gRestir.writeSlice + 2u) + px;
            gHistPos[writeDiAt] = float4(max(curIrrad, 0.0), diAge);
            add = max(curIrrad * demod, 0.0);
        }
    } else {
        uint2 writeDiAt = restirSliceOrigin(gRestir.writeSlice + 2u) + px;
        gHistPos[writeDiAt] = float4(0.0, 0.0, 0.0, 0.0);
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

// Пространственно-временной синий шум / IGN для гладких полутеней солнечного диска
float3 restirSunDir(uint2 px, float3 L, uint ray) {
    float halfA = radians(max(gRestir.sunDisk, 0.0)) * 0.5;
    if (halfA < 1e-5) {
        return L;
    }
    float2 xi = restirBlueNoise2(px, gRestir.runtime.frameIndex, 19u + ray);
    float radius = sqrt(xi.x) * halfA;
    float phi = xi.y * 6.2831853;
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
    float ndl = dot(surface.geoNormal, L);
    if (ndl <= 0.0) {
        restirSigmaStore(px, 0.0, 0.0, 0.0, 1.0);
        return;
    }
    if ((gFrame.debugPad0 & 0x80000000u) != 0u) {
        RAB_LightSample open = RAB_EmptyLightSample();
        open.position = surface.worldPos + L * 20000.0;
        open.normal = -L;
        open.radiance = max(gSun.sunColor.rgb, 0.0) * max(gSun.sunDirIntensity.w, 0.0);
        open.solidAnglePdf = 1.0;
        float3 openShade = max(restirShade(open, surface), 0.0);
        restirSigmaStore(px, 1.0, 0.0, openShade, 1.0);
        return;
    }
    float3 origin = surface.worldPos + surface.geoNormal * 0.02 + L * 0.01;
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

    // Точный перевод мирового размера полутени в экранный радиус в пикселях для денойзера Sigma
    float halfA = radians(max(gRestir.sunDisk, 0.0)) * 0.5;
    float penumbraPx = 0.0;
    if (closest >= 0.0) {
        float worldPenumbra = closest * tan(halfA);
        penumbraPx = clamp(worldPenumbra / max(surface.viewDepth, 0.1) * float(gFrame.extent.y) * 0.5, 0.0, 16.0);
    }

    RAB_LightSample sample = RAB_EmptyLightSample();
    sample.position = surface.worldPos + L * 20000.0;
    sample.normal = -L;
    sample.radiance = max(gSun.sunColor.rgb, 0.0) * max(gSun.sunDirIntensity.w, 0.0);
    sample.solidAnglePdf = 1.0;
    float3 shade = max(restirShade(sample, surface), 0.0);
    restirSigmaStore(px, vis, penumbraPx, shade, 1.0);
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