// SHaRC 1.8.3 (NVIDIA RTXGI порт).

static const uint kSharcPosBits = 17u;
static const uint kSharcLevelBits = 9u;
static const uint kSharcNormalBits = 3u;
static const uint kSharcPosMask = (1u << kSharcPosBits) - 1u;
static const uint kSharcLevelMask = (1u << kSharcLevelBits) - 1u;
static const uint kSharcNormalMask = (1u << kSharcNormalBits) - 1u;
static const uint kSharcLevelShift = kSharcPosBits * 3u;
static const uint kSharcNormalShift = kSharcLevelShift + kSharcLevelBits;
static const uint kSharcBucket = 16u;
static const uint kSharcProbe = 8u;
static const uint kSharcEvictStale = 24u;
static const uint kSharcInvalid = 0xffffffffu;
static const uint kSharcEmptyLimit = 2u;
static const float kSharcPosBias = 1e-4;
static const float kSharcNormalBias = 1e-3;
static const float kSharcLogBase = 2.0;
static const float kSharcBaseVoxel = 0.40;
static const uint kSharcDepth = 2u;
static const uint kSharcResampleMin = 1u;
static const uint kSharcProbeWindow = 8u;
static const uint kSharcStaleMin = 16u;
static const uint kSharcStaleMax = 1024u;
static const uint kSharcAccumMin = 1u;
static const uint kSharcAccumMax = 1024u;
static const float kSharcSampleGate = 0.0;

struct SharcGrid {
    float3 camera;
    float logBase;
    float sceneScale;
    float levelBias;
};

struct SharcState {
    uint cacheIndices[kSharcDepth];
    float3 sampleWeights[kSharcDepth];
    uint pathLength;
};

struct SharcHitData {
    float3 positionWorld;
    float3 normalWorld;
    float3 materialDemodulation;
};

uint sharcJenkins(uint a) {
    a = (a + 0x7ed55d16u) + (a << 12);
    a = (a ^ 0xc761c23cu) ^ (a >> 19);
    a = (a + 0x165667b1u) + (a << 5);
    a = (a + 0xd3a2646cu) ^ (a << 9);
    a = (a + 0xfd7046c5u) + (a << 3);
    a = (a ^ 0xb55a4f09u) ^ (a >> 16);
    return a;
}

uint sharcHash32(uint64_t key) {
    return sharcJenkins(uint(key)) ^ sharcJenkins(uint(key >> 32));
}

uint sharcBaseSlot(uint64_t key, uint capacity) {
    uint slots = capacity - kSharcBucket + 1u;
    return sharcHash32(key) % slots;
}

float sharcLevelFloat(float3 position, SharcGrid grid) {
    float baseRadius = max(grid.sceneScale * 0.20, 6.0);
    float dist = length(position - grid.camera);
    float normDist = max(dist / baseRadius, 1.0);
    return clamp(log2(normDist) + grid.levelBias + 1.0, 1.0, 10.0);
}

uint sharcLevel(float3 position, SharcGrid grid) {
    return uint(floor(sharcLevelFloat(position, grid)));
}

float sharcVoxelSize(uint level, SharcGrid grid) {
    uint clamped = clamp(level, 1u, 12u);
    return kSharcBaseVoxel * exp2(float(int(clamped) - 1));
}

bool sharcCacheEligible(float hitDistance, float3 position, float roughPrev, SharcGrid grid) {
    float voxel = sharcVoxelSize(sharcLevel(position, grid), grid);
    if (hitDistance <= voxel * 1.2) {
        return false;
    }
    float rough = min(max(roughPrev, 0.15), 0.99);
    float alpha = rough * rough;
    float denom = max(1.0 - alpha * alpha, 1e-4);
    float spread = sqrt((0.5 * alpha * alpha) / denom);
    return hitDistance * spread > voxel * 0.5;
}

int sharcSignPos(int packed) {
    return (packed << (32 - int(kSharcPosBits))) >> (32 - int(kSharcPosBits));
}

uint sharcNormalAxis(float3 normal) {
    float3 a = abs(normal);
    if (a.y >= a.x && a.y >= a.z) {
        return normal.y >= 0.0 ? 2u : 3u;
    }
    if (a.x >= a.z) {
        return normal.x >= 0.0 ? 0u : 1u;
    }
    return normal.z >= 0.0 ? 4u : 5u;
}

uint64_t sharcCellKey(int3 cell, uint level, uint normalBits) {
    return (uint64_t(uint(cell.x) & kSharcPosMask) << (kSharcPosBits * 0u))
        | (uint64_t(uint(cell.y) & kSharcPosMask) << (kSharcPosBits * 1u))
        | (uint64_t(uint(cell.z) & kSharcPosMask) << (kSharcPosBits * 2u))
        | (uint64_t(level & kSharcLevelMask) << kSharcLevelShift)
        | (uint64_t(normalBits & kSharcNormalMask) << kSharcNormalShift);
}

uint64_t sharcSpatialKeyLevel(float3 position, float3 normal, uint level, SharcGrid grid, out float voxelSize) {
    voxelSize = sharcVoxelSize(level, grid);
    float3 biased = position + normal * (voxelSize * 0.05) + float3(kSharcPosBias, kSharcPosBias, kSharcPosBias);
    int3 cell = int3(floor(biased / voxelSize));
    return sharcCellKey(cell, level, sharcNormalAxis(normal));
}

uint64_t sharcSpatialKey(float3 position, float3 normal, SharcGrid grid, out float voxelSize) {
    uint level = sharcLevel(position, grid);
    return sharcSpatialKeyLevel(position, normal, level, grid, voxelSize);
}

void sharcLoadMeta(uint entry, out uint frames, out uint stale);
void sharcStoreAcc(uint entry, uint4 value);
void sharcStoreResolved(uint entry, float3 radiance, float sampleNum, uint frames, uint stale);
void sharcAddVoxel(uint cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData);

void sharcCas(uint slot, uint64_t compareValue, uint64_t value, out uint64_t original) {
    InterlockedCompareExchange(gHash[slot], compareValue, value, original);
}

bool sharcInsert(uint64_t key, uint baseSlot, uint probeRange, out uint cacheIndex) {
    cacheIndex = kSharcInvalid;
    uint steps = min(probeRange, kSharcProbe);
    [loop]
    for (uint offset = 0u; offset < steps; ++offset) {
        uint slot = baseSlot + offset;
        uint64_t previous = 0;
        sharcCas(slot, uint64_t(0), key, previous);
        if (previous == uint64_t(0) || previous == key) {
            cacheIndex = slot;
            return true;
        }
        uint frames = 0u;
        uint stale = 0u;
        sharcLoadMeta(slot, frames, stale);
        if (stale < kSharcEvictStale) {
            continue;
        }
        uint64_t raced = 0;
        sharcCas(slot, previous, key, raced);
        if (raced != previous) {
            continue;
        }
        sharcStoreAcc(slot, uint4(0, 0, 0, 0));
        sharcStoreResolved(slot, float3(0.0, 0.0, 0.0), 0.0, 0u, 0u);
        cacheIndex = slot;
        return true;
    }
    return false;
}

bool sharcFind(uint64_t key, uint baseSlot, uint probeRange, out uint cacheIndex, out uint bucketOffset) {
    cacheIndex = kSharcInvalid;
    bucketOffset = 0u;
    uint steps = min(probeRange, kSharcProbe);
    [loop]
    for (bucketOffset = 0u; bucketOffset < steps; ++bucketOffset) {
        if (gHash[baseSlot + bucketOffset] == key) {
            cacheIndex = baseSlot + bucketOffset;
            return true;
        }
    }
    return false;
}

bool sharcInsertEntryLevel(float3 position, float3 normal, uint level, SharcGrid grid, out uint64_t key, out uint cacheIndex) {
    float voxelSize = 0.0;
    key = sharcSpatialKeyLevel(position, normal, level, grid, voxelSize);
    return sharcInsert(key, sharcBaseSlot(key, gSharc.capacity), kSharcBucket, cacheIndex);
}

bool sharcInsertEntry(float3 position, float3 normal, SharcGrid grid, out uint64_t key, out uint cacheIndex) {
    return sharcInsertEntryLevel(position, normal, sharcLevel(position, grid), grid, key, cacheIndex);
}

float sharcLuma(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

uint sharcResByte(uint entry) {
    return entry * 32u;
}

float4 sharcLoadRadiance(uint entry) {
    uint o = sharcResByte(entry);
    return float4(
        asfloat(gResolved.Load(o)),
        asfloat(gResolved.Load(o + 4u)),
        asfloat(gResolved.Load(o + 8u)),
        asfloat(gResolved.Load(o + 12u)));
}

void sharcLoadMeta(uint entry, out uint frames, out uint stale) {
    uint o = sharcResByte(entry) + 16u;
    frames = gResolved.Load(o);
    stale = gResolved.Load(o + 4u);
}

void sharcStoreResolved(uint entry, float3 radiance, float sampleNum, uint frames, uint stale) {
    uint o = sharcResByte(entry);
    gResolved.Store(o, asuint(max(radiance.r, 0.0)));
    gResolved.Store(o + 4u, asuint(max(radiance.g, 0.0)));
    gResolved.Store(o + 8u, asuint(max(radiance.b, 0.0)));
    gResolved.Store(o + 12u, asuint(max(sampleNum, 0.0)));
    gResolved.Store(o + 16u, frames);
    gResolved.Store(o + 20u, stale);
    gResolved.Store(o + 24u, 0u);
    gResolved.Store(o + 28u, 0u);
}

uint sharcAccByte(uint entry) {
    return entry * 16u;
}

uint4 sharcLoadAcc(uint entry) {
    uint o = sharcAccByte(entry);
    return uint4(gAcc.Load(o), gAcc.Load(o + 4u), gAcc.Load(o + 8u), gAcc.Load(o + 12u));
}

void sharcStoreAcc(uint entry, uint4 value) {
    uint o = sharcAccByte(entry);
    gAcc.Store(o, value.x);
    gAcc.Store(o + 4u, value.y);
    gAcc.Store(o + 8u, value.z);
    gAcc.Store(o + 12u, value.w);
}

uint sharcQuant(float value) {
    if (value <= 0.0 || isnan(value) || isinf(value)) {
        return 0u;
    }
    uint q = uint(min(value, 32768.0) + 0.5);
    return q > 0u ? q : 1u;
}

float3 sharcClampFirefly(float3 sampleValue) {
    if (any(isnan(sampleValue)) || any(isinf(sampleValue))) {
        return 0.0;
    }
    float3 color = clamp(sampleValue, 0.0, 8.0);
    float luma = sharcLuma(color);
    if (luma > 8.0) {
        color *= 8.0 / luma;
    }
    return color;
}

SharcGrid sharcGrid() {
    SharcGrid grid;
    grid.camera = float3(gSharc.camX, gSharc.camY, gSharc.camZ);
    grid.logBase = kSharcLogBase;
    grid.sceneScale = gSharc.sceneScale;
    grid.levelBias = gSharc.levelBias;
    return grid;
}

void sharcAddVoxel(uint cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData) {
    if (cacheIndex == kSharcInvalid) {
        return;
    }
    float scale = clamp(gSharc.radianceScale, 100.0, 2000.0);
    float3 weighted = sharcClampFirefly(sampleValue * sampleWeight) * scale;
    uint o = sharcAccByte(cacheIndex);
    uint ignored = 0u;
    uint red = sharcQuant(weighted.r);
    uint green = sharcQuant(weighted.g);
    uint blue = sharcQuant(weighted.b);
    if (red != 0u) {
        gAcc.InterlockedAdd(o, red, ignored);
    }
    if (green != 0u) {
        gAcc.InterlockedAdd(o + 4u, green, ignored);
    }
    if (blue != 0u) {
        gAcc.InterlockedAdd(o + 8u, blue, ignored);
    }
    if (sampleData != 0u) {
        gAcc.InterlockedAdd(o + 12u, sampleData, ignored);
    }
}

// Заполняет всю касательную окрестность 3x3 и родительский каскад LoD+1 без вырезания строк на стенах
void sharcPaintEnergy(float3 position, float3 normal, float3 energy, bool spread) {
    SharcGrid grid = sharcGrid();
    uint level = sharcLevel(position, grid);
    float voxel = max(sharcVoxelSize(level, grid), 1e-3);
    float3 n = dot(normal, normal) > 1e-8 ? normalize(normal) : float3(0.0, 1.0, 0.0);
    float3 axis = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(axis, n));
    float3 bitangent = cross(n, tangent);

    int reach = spread ? 1 : 0;
    [loop]
    for (int y = -reach; y <= reach; ++y) {
        [loop]
        for (int x = -reach; x <= reach; ++x) {
            float3 at = position + (tangent * float(x) + bitangent * float(y)) * (voxel * 0.85);
            uint64_t key = 0;
            uint cacheIndex = kSharcInvalid;
            if (sharcInsertEntryLevel(at, n, level, grid, key, cacheIndex)) {
                sharcAddVoxel(cacheIndex, energy, float3(1.0, 1.0, 1.0), 1u);
            }
        }
    }

    uint coarseLevel = min(level + 1u, 10u);
    if (coarseLevel != level) {
        float coarseVoxel = max(sharcVoxelSize(coarseLevel, grid), 1e-3);
        [loop]
        for (int cy = -reach; cy <= reach; ++cy) {
            [loop]
            for (int cx = -reach; cx <= reach; ++cx) {
                float3 atCoarse = position + (tangent * float(cx) + bitangent * float(cy)) * (coarseVoxel * 0.75);
                uint64_t coarseKey = 0;
                uint coarseIdx = kSharcInvalid;
                if (sharcInsertEntryLevel(atCoarse, n, coarseLevel, grid, coarseKey, coarseIdx)) {
                    sharcAddVoxel(coarseIdx, energy, float3(1.0, 1.0, 1.0), 1u);
                }
            }
        }
    }
}

void sharcFlushWave(uint cacheIndex, uint4 packed) {
}

void sharcInit(inout SharcState state) {
    state.pathLength = 0u;
    [unroll]
    for (uint i = 0u; i < kSharcDepth; ++i) {
        state.cacheIndices[i] = kSharcInvalid;
        state.sampleWeights[i] = 0.0;
    }
}

void sharcUpdateMiss(SharcState state, float3 radiance) {
    [loop]
    for (uint i = 0u; i < state.pathLength; ++i) {
        sharcAddVoxel(state.cacheIndices[i], radiance, state.sampleWeights[i], 0u);
    }
}

bool sharcUpdateHit(inout SharcState state, SharcHitData hit, float3 directLighting, float random, SharcGrid grid) {
    uint64_t key = 0;
    uint cacheIndex = kSharcInvalid;
    if (!sharcInsertEntry(hit.positionWorld, hit.normalWorld, grid, key, cacheIndex)) {
        return false;
    }
    float3 demod = clamp(hit.materialDemodulation, float3(0.1, 0.1, 0.1), float3(1.0, 1.0, 1.0));
    float3 inverseDemod = 1.0 / demod;
    float3 radiance = directLighting;
    bool keepTracing = true;
    uint resampleDepth = uint(round(lerp(float(kSharcResampleMin), float(kSharcDepth), random)));
    if (resampleDepth <= state.pathLength) {
        float4 stored = sharcLoadRadiance(cacheIndex);
        if (stored.w > 4.0) {
            radiance = max(stored.xyz, 0.0) * demod;
            keepTracing = false;
        }
    }
    if (keepTracing) {
        float3 contrib = max(directLighting * inverseDemod, 0.0);
        sharcAddVoxel(cacheIndex, contrib, float3(1.0, 1.0, 1.0), 1u);
    }
    [loop]
    for (uint i = 0u; i < state.pathLength; ++i) {
        uint previous = state.cacheIndices[i];
        if (previous == kSharcInvalid) {
            continue;
        }
        sharcAddVoxel(previous, radiance, state.sampleWeights[i], 0u);
    }
    [loop]
    for (uint shift = min(state.pathLength, kSharcDepth - 1u); shift > 0u; --shift) {
        state.cacheIndices[shift] = state.cacheIndices[shift - 1u];
        state.sampleWeights[shift] = state.sampleWeights[shift - 1u];
    }
    state.cacheIndices[0] = cacheIndex;
    state.sampleWeights[0] = inverseDemod;
    state.pathLength = min(state.pathLength + 1u, kSharcDepth);
    return keepTracing;
}

void sharcSetThroughput(inout SharcState state, float3 throughput) {
    [loop]
    for (uint i = 0u; i < state.pathLength; ++i) {
        state.sampleWeights[i] *= throughput;
    }
}

bool sharcLookupCell(int3 cell, uint level, uint normalBits, out float3 radiance, out float sampleCount) {
    radiance = 0.0;
    sampleCount = 0.0;
    uint64_t key = sharcCellKey(cell, level, normalBits);
    uint cacheIndex = kSharcInvalid;
    uint bucketOffset = 0u;
    if (!sharcFind(key, sharcBaseSlot(key, gSharc.capacity), kSharcBucket, cacheIndex, bucketOffset)) {
        return false;
    }
    float4 stored = sharcLoadRadiance(cacheIndex);
    if (stored.w <= kSharcSampleGate || any(isnan(stored.xyz)) || any(isinf(stored.xyz))) {
        return false;
    }
    radiance = max(stored.xyz, 0.0);
    sampleCount = stored.w;
    return true;
}

bool sharcSampleLevelTrilinear(float3 position, float3 normal, uint level, SharcGrid grid, out float3 outRad, out float outConf) {
    outRad = 0.0;
    outConf = 0.0;
    float voxelSize = sharcVoxelSize(level, grid);
    float3 biased = position + normal * (voxelSize * 0.05) + float3(kSharcPosBias, kSharcPosBias, kSharcPosBias);
    float3 local = biased / max(voxelSize, 1e-6) - 0.5;
    int3 baseCell = int3(floor(local));
    float3 f = saturate(local - float3(baseCell));
    uint normalBits = sharcNormalAxis(normal);

    float3 sum = 0.0;
    float weightSum = 0.0;
    float maxSamples = 0.0;

    [unroll]
    for (uint z = 0u; z < 2u; ++z) {
        [unroll]
        for (uint y = 0u; y < 2u; ++y) {
            [unroll]
            for (uint x = 0u; x < 2u; ++x) {
                float trilinearW = (x == 0u ? 1.0 - f.x : f.x)
                                 * (y == 0u ? 1.0 - f.y : f.y)
                                 * (z == 0u ? 1.0 - f.z : f.z);
                if (trilinearW < 1e-4) {
                    continue;
                }
                float3 corner = 0.0;
                float samples = 0.0;
                if (!sharcLookupCell(baseCell + int3(int(x), int(y), int(z)), level, normalBits, corner, samples)) {
                    continue;
                }
                float sampleWeight = saturate(samples / 4.0);
                float w = trilinearW * max(sampleWeight, 0.25);
                sum += corner * w;
                weightSum += w;
                maxSamples = max(maxSamples, samples);
            }
        }
    }

    if (weightSum <= 1e-5) {
        int3 directCell = int3(floor(biased / max(voxelSize, 1e-6)));
        float3 directRad = 0.0;
        float directSamples = 0.0;
        if (sharcLookupCell(directCell, level, normalBits, directRad, directSamples)) {
            outRad = directRad;
            outConf = saturate(directSamples / 4.0);
            return true;
        }
        return false;
    }

    outRad = sum / weightSum;
    outConf = saturate(maxSamples / 6.0);
    return true;
}

bool sharcGetCachedRadiance(SharcHitData hit, SharcGrid grid, out float3 radiance) {
    radiance = 0.0;
    float lvlF = sharcLevelFloat(hit.positionWorld, grid);
    uint lvl0 = uint(floor(lvlF));
    uint lvl1 = min(lvl0 + 1u, 10u);
    float lvlFrac = saturate(lvlF - float(lvl0));

    float3 rad0 = 0.0;
    float conf0 = 0.0;
    bool ok0 = sharcSampleLevelTrilinear(hit.positionWorld, hit.normalWorld, lvl0, grid, rad0, conf0);

    float3 rad1 = 0.0;
    float conf1 = 0.0;
    bool ok1 = (lvl1 != lvl0) && sharcSampleLevelTrilinear(hit.positionWorld, hit.normalWorld, lvl1, grid, rad1, conf1);

    float3 combined = 0.0;
    bool found = false;

    if (ok0 && ok1) {
        float blendToCoarse = max(lvlFrac, (1.0 - conf0) * 0.7);
        combined = lerp(rad0, rad1, blendToCoarse);
        found = true;
    } else if (ok0) {
        combined = rad0;
        found = true;
    } else if (ok1) {
        combined = rad1;
        found = true;
    } else {
        uint lvl2 = min(lvl0 + 2u, 10u);
        float3 rad2 = 0.0;
        float conf2 = 0.0;
        if (sharcSampleLevelTrilinear(hit.positionWorld, hit.normalWorld, lvl2, grid, rad2, conf2)) {
            combined = rad2;
            found = true;
        }
    }

    if (!found) {
        return false;
    }

    radiance = sharcClampFirefly(max(combined, 0.0)) * hit.materialDemodulation;
    return true;
}

void sharcResolveEntry(uint entryIndex) {
    if (entryIndex >= gSharc.capacity) {
        return;
    }
    uint64_t key = gHash[entryIndex];
    if (key == uint64_t(0)) {
        return;
    }
    uint4 accumulated = sharcLoadAcc(entryIndex);
    float4 resolvedRad = sharcLoadRadiance(entryIndex);
    uint accumulatedFrames = 0u;
    uint staleFrames = 0u;
    sharcLoadMeta(entryIndex, accumulatedFrames, staleFrames);
    float sampleNum = float(accumulated.w);
    float sampleNumPrev = resolvedRad.w;
    accumulatedFrames += 1u;
    staleFrames = sampleNum != 0.0 ? 0u : staleFrames + 1u;
    uint staleLimit = uint(clamp(gSharc.staleFrameNum, kSharcStaleMin, kSharcStaleMax));
    if (staleFrames >= staleLimit) {
        gHash[entryIndex] = uint64_t(0);
        sharcStoreAcc(entryIndex, uint4(0, 0, 0, 0));
        sharcStoreResolved(entryIndex, float3(0.0, 0.0, 0.0), 0.0, 0u, 0u);
        return;
    }
    if (sampleNum == 0.0) {
        sharcStoreResolved(entryIndex, resolvedRad.xyz, sampleNumPrev, accumulatedFrames, staleFrames);
        return;
    }
    float radScale = clamp(gSharc.radianceScale, 100.0, 2000.0);
    float scale = 1.0 / max(radScale * max(sampleNum, 1.0), 1e-6);
    float3 radianceNow = float3(float(accumulated.x), float(accumulated.y), float(accumulated.z)) * scale;
    radianceNow = sharcClampFirefly(radianceNow);

    float3 radiancePrev = resolvedRad.xyz;
    if (any(isnan(radiancePrev)) || any(isinf(radiancePrev))) {
        radiancePrev = 0.0;
        sampleNumPrev = 0.0;
    }

    uint accumulationLimit = uint(clamp(gSharc.accumulationFrameNum, kSharcAccumMin, kSharcAccumMax));
    if (accumulatedFrames > accumulationLimit) {
        float normalize = float(accumulationLimit) / float(accumulatedFrames);
        accumulatedFrames = accumulationLimit;
        sampleNumPrev *= normalize;
    }
    float accumulatedSamples = min(sampleNumPrev + sampleNum, 512.0);
    float3 blended = radianceNow;
    if (sampleNumPrev > 0.0) {
        float alpha = clamp(1.0 / float(max(accumulatedFrames, 1u)), 0.02, 0.25);
        blended = lerp(radiancePrev, radianceNow, alpha);
    }

    sharcStoreResolved(entryIndex, blended, accumulatedSamples, accumulatedFrames, staleFrames);
    sharcStoreAcc(entryIndex, uint4(0, 0, 0, 0));
}

float3 sharcHashColor(float3 position, float3 normal) {
    float voxelSize = 0.0;
    uint64_t key = sharcSpatialKey(position, normal, sharcGrid(), voxelSize);
    uint hash = sharcHash32(key);
    uint levelHash = sharcJenkins(sharcLevel(position, sharcGrid()));
    float3 color = float3(
        float((hash >> 0) & 0x3ffu) / 1023.0,
        float((hash >> 11) & 0x7ffu) / 2047.0,
        float((hash >> 22) & 0x7ffu) / 2047.0);
    float3 level = float3(
        float((levelHash >> 0) & 0x3ffu) / 1023.0,
        float((levelHash >> 11) & 0x7ffu) / 2047.0,
        float((levelHash >> 22) & 0x7ffu) / 2047.0);
    return color * level;
}