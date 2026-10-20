// SHaRC 1.8.3. Порт RTXGI Libraries/Sharc (HashGridCommon.h, SharcCommon.h, SharcTypes.h)
// при дефайнах сэмпла Pathtracer: 64-битный ключ, 64-битный CAS, нормали в ключе,
// без SH, без responsive lighting, separate emissive выключен, material demodulation включена,
// resampling на update, propagation depth 2, blend соседних уровней.
// Resolved — 32 байта: три float rgb, float числа проб, кадры и stale. Упаковки fp16 нет.
// Буферы объявляет проход до include: gHash, gAcc, gResolved, gSharc.

static const uint kSharcPosBits = 17u;
static const uint kSharcLevelBits = 9u;
static const uint kSharcNormalBits = 3u;
static const uint kSharcPosMask = (1u << kSharcPosBits) - 1u;
static const uint kSharcLevelMask = (1u << kSharcLevelBits) - 1u;
static const uint kSharcNormalMask = (1u << kSharcNormalBits) - 1u;
static const uint kSharcLevelShift = kSharcPosBits * 3u;
static const uint kSharcNormalShift = kSharcLevelShift + kSharcLevelBits;
static const uint kSharcBucket = 16u;
static const uint kSharcProbe = 3u;
static const uint kSharcEvictStale = 16u;
static const uint kSharcInvalid = 0xffffffffu;
static const uint kSharcEmptyLimit = 2u;
static const float kSharcPosBias = 1e-4;
static const float kSharcNormalBias = 1e-3;
static const float kSharcLogBase = 2.0;
static const uint kSharcDepth = 2u;
static const uint kSharcResampleMin = 1u;
static const uint kSharcProbeWindow = 8u;
static const uint kSharcStaleMin = 8u;
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

uint sharcLevel(float3 position, SharcGrid grid) {
    return 1u;
}

float sharcVoxelSize(uint level, SharcGrid grid) {
    return 0.5;
}

bool sharcCacheEligible(float hitDistance, float3 position, float roughPrev, SharcGrid grid) {
    float voxel = sharcVoxelSize(sharcLevel(position, grid), grid);
    if (hitDistance <= voxel * 1.7320508) {
        return false;
    }
    float rough = min(max(roughPrev, 0.0), 0.99);
    float alpha = rough * rough;
    float denom = max(1.0 - alpha * alpha, 1e-4);
    float spread = sqrt((0.5 * alpha * alpha) / denom);
    return hitDistance * spread > voxel;
}

int sharcSignPos(int packed) {
    return (packed << (32 - int(kSharcPosBits))) >> (32 - int(kSharcPosBits));
}

uint sharcNormalAxis(float3 normal) {
    float3 a = abs(normal);
    if (a.y >= a.x && a.y >= a.z) {
        return 1u;
    }
    return a.x >= a.z ? 0u : 2u;
}

uint64_t sharcSpatialKey(float3 position, float3 normal, SharcGrid grid, out float voxelSize) {
    float3 biased = position + float3(kSharcPosBias, kSharcPosBias, kSharcPosBias);
    uint level = sharcLevel(biased, grid);
    voxelSize = sharcVoxelSize(level, grid);
    int3 cell = int3(floor(biased / voxelSize));
    uint64_t key = (uint64_t(uint(cell.x) & kSharcPosMask) << (kSharcPosBits * 0u))
        | (uint64_t(uint(cell.y) & kSharcPosMask) << (kSharcPosBits * 1u))
        | (uint64_t(uint(cell.z) & kSharcPosMask) << (kSharcPosBits * 2u))
        | (uint64_t(level & kSharcLevelMask) << kSharcLevelShift);
    key |= uint64_t(sharcNormalAxis(normal)) << kSharcNormalShift;
    return key;
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

bool sharcInsertEntry(float3 position, float3 normal, SharcGrid grid, out uint64_t key, out uint cacheIndex) {
    float voxelSize = 0.0;
    key = sharcSpatialKey(position, normal, grid, voxelSize);
    return sharcInsert(key, sharcBaseSlot(key, gSharc.capacity), kSharcBucket, cacheIndex);
}

float sharcLuma(float3 color) {
    return dot(color, float3(0.213, 0.715, 0.072));
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
    if (value <= 0.0) {
        return 0u;
    }
    uint q = uint(value + 0.5);
    return q > 0u ? q : 1u;
}

float3 sharcClampFirefly(float3 sampleValue) {
    float3 color = max(sampleValue, 0.0);
    float luma = sharcLuma(color);
    if (luma > 20.0) {
        color *= 20.0 / luma;
    }
    return color;
}

bool sharcQueueRadiance(SharcHitData hit, float3 directLighting, SharcGrid grid, out uint cacheIndex, out uint4 packed) {
    cacheIndex = kSharcInvalid;
    packed = 0;
    uint64_t key = 0;
    if (!sharcInsertEntry(hit.positionWorld, hit.normalWorld, grid, key, cacheIndex)) {
        cacheIndex = kSharcInvalid;
        return false;
    }
    float3 demod = max(hit.materialDemodulation, float3(1e-3, 1e-3, 1e-3));
    float3 contrib = max(directLighting / demod, 0.0);
    if (sharcLuma(contrib) <= 1e-4) {
        cacheIndex = kSharcInvalid;
        return false;
    }
    float3 weighted = sharcClampFirefly(contrib) * gSharc.radianceScale;
    packed = uint4(sharcQuant(weighted.r), sharcQuant(weighted.g), sharcQuant(weighted.b), 1u);
    return true;
}

void sharcPaintEnergy(float3 position, float3 normal, float3 energy, bool spread) {
    SharcGrid grid = sharcGrid();
    uint level = sharcLevel(position, grid);
    float voxel = max(sharcVoxelSize(level, grid), 1e-3);
    float3 n = dot(normal, normal) > 1e-8 ? normalize(normal) : float3(0.0, 1.0, 0.0);
    float3 axis = abs(n.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(axis, n));
    float3 bitangent = cross(n, tangent);
    int reach = spread ? 1 : 0;
    int homeY = int(floor(position.y / voxel));
    bool wall = abs(n.y) < 0.7;
    [loop]
    for (int y = -reach; y <= reach; ++y) {
        [loop]
        for (int x = -reach; x <= reach; ++x) {
            float3 at = position + (tangent * float(x) + bitangent * float(y)) * voxel;
            if (wall && int(floor(at.y / voxel)) != homeY) {
                continue;
            }
            SharcHitData hit;
            hit.positionWorld = at;
            hit.normalWorld = n;
            hit.materialDemodulation = float3(1.0, 1.0, 1.0);
            uint cacheIndex = kSharcInvalid;
            uint4 packed = 0;
            if (sharcQueueRadiance(hit, energy, grid, cacheIndex, packed)) {
                sharcAddVoxel(cacheIndex, energy, float3(1.0, 1.0, 1.0), 1u);
            }
        }
    }
}

void sharcFlushWave(uint cacheIndex, uint4 packed) {
    uint lane = WaveGetLaneIndex();
    uint lanes = WaveGetLaneCount();
    uint first = lane;
    uint4 sum = 0;
    [loop]
    for (uint i = 0u; i < lanes; ++i) {
        uint other = WaveReadLaneAt(cacheIndex, i);
        uint4 part = WaveReadLaneAt(packed, i);
        if (other == cacheIndex) {
            if (i < first) {
                first = i;
            }
            sum += part;
        }
    }
    if (first != lane || cacheIndex == kSharcInvalid) {
        return;
    }
    uint o = sharcAccByte(cacheIndex);
    uint ignored = 0u;
    if (sum.x != 0u) {
        gAcc.InterlockedAdd(o, sum.x, ignored);
    }
    if (sum.y != 0u) {
        gAcc.InterlockedAdd(o + 4u, sum.y, ignored);
    }
    if (sum.z != 0u) {
        gAcc.InterlockedAdd(o + 8u, sum.z, ignored);
    }
    if (sum.w != 0u) {
        gAcc.InterlockedAdd(o + 12u, sum.w, ignored);
    }
}

void sharcAddVoxel(uint cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData) {
    float3 weighted = sharcClampFirefly(sampleValue * sampleWeight) * gSharc.radianceScale;
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
    float3 demod = max(hit.materialDemodulation, float3(1e-3, 1e-3, 1e-3));
    float3 inverseDemod = 1.0 / demod;
    float3 radiance = directLighting;
    bool keepTracing = true;
    uint resampleDepth = uint(round(lerp(float(kSharcResampleMin), float(kSharcDepth), random)));
    if (resampleDepth <= state.pathLength) {
        float4 stored = sharcLoadRadiance(cacheIndex);
        if (stored.w > kSharcSampleGate) {
            radiance = max(stored.xyz, 0.0) * demod;
            keepTracing = false;
        }
    }
    if (keepTracing) {
        float3 contrib = max(directLighting * inverseDemod, 0.0);
        if (sharcLuma(contrib) > 1e-4) {
            sharcAddVoxel(cacheIndex, contrib, float3(1.0, 1.0, 1.0), 1u);
        }
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

uint64_t sharcCellKey(int3 cell, uint level, uint normalBits) {
    return (uint64_t(uint(cell.x) & kSharcPosMask) << (kSharcPosBits * 0u))
        | (uint64_t(uint(cell.y) & kSharcPosMask) << (kSharcPosBits * 1u))
        | (uint64_t(uint(cell.z) & kSharcPosMask) << (kSharcPosBits * 2u))
        | (uint64_t(level & kSharcLevelMask) << kSharcLevelShift)
        | (uint64_t(normalBits) << kSharcNormalShift);
}

bool sharcLookupCell(int3 cell, uint level, uint normalBits, out float3 radiance) {
    radiance = 0.0;
    uint64_t key = sharcCellKey(cell, level, normalBits);
    uint cacheIndex = kSharcInvalid;
    uint bucketOffset = 0u;
    if (!sharcFind(key, sharcBaseSlot(key, gSharc.capacity), kSharcBucket, cacheIndex, bucketOffset)) {
        return false;
    }
    float4 stored = sharcLoadRadiance(cacheIndex);
    if (stored.w <= kSharcSampleGate) {
        return false;
    }
    radiance = max(stored.xyz, 0.0);
    return true;
}

bool sharcGetCachedRadiance(SharcHitData hit, SharcGrid grid, out float3 radiance) {
    radiance = 0.0;
    float3 biased = hit.positionWorld + float3(kSharcPosBias, kSharcPosBias, kSharcPosBias);
    uint level = sharcLevel(biased, grid);
    float voxelSize = sharcVoxelSize(level, grid);
    float3 local = biased / max(voxelSize, 1e-6);
    int3 cell = int3(floor(local));
    float3 frac = local - float3(cell);
    uint normalBits = sharcNormalAxis(hit.normalWorld);
    float3 sum = 0.0;
    float weightSum = 0.0;
    [unroll]
    for (uint z = 0u; z < 2u; ++z) {
        [unroll]
        for (uint y = 0u; y < 2u; ++y) {
            [unroll]
            for (uint x = 0u; x < 2u; ++x) {
                float weight = (x == 0u ? 1.0 - frac.x : frac.x)
                    * (y == 0u ? 1.0 - frac.y : frac.y)
                    * (z == 0u ? 1.0 - frac.z : frac.z);
                if (weight < 1e-5) {
                    continue;
                }
                float3 corner = 0.0;
                if (!sharcLookupCell(cell + int3(int(x), int(y), int(z)), level, normalBits, corner)) {
                    continue;
                }
                sum += corner * weight;
                weightSum += weight;
            }
        }
    }
    if (weightSum <= 0.0) {
        [unroll]
        for (uint side = 0u; side < 2u; ++side) {
            uint other = side == 0u ? (level > 1u ? level - 1u : 1u) : min(level + 1u, 5u);
            if (other == level) {
                continue;
            }
            float otherSize = sharcVoxelSize(other, grid);
            int3 otherCell = int3(floor(biased / max(otherSize, 1e-6)));
            float3 corner = 0.0;
            if (sharcLookupCell(otherCell, other, normalBits, corner)) {
                sum += corner;
                weightSum += 1.0;
            }
        }
    }
    if (weightSum <= 0.0) {
        return false;
    }
    radiance = (sum / weightSum) * hit.materialDemodulation;
    return true;
}

int sharcGridDistance2(int3 position) {
    return position.x * position.x + position.y * position.y + position.z * position.z;
}

uint64_t sharcAdjacentKey(uint64_t key, SharcGrid grid, float3 cameraPrev) {
    int3 cell = int3(
        sharcSignPos(int((key >> (kSharcPosBits * 0u)) & uint64_t(kSharcPosMask))),
        sharcSignPos(int((key >> (kSharcPosBits * 1u)) & uint64_t(kSharcPosMask))),
        sharcSignPos(int((key >> (kSharcPosBits * 2u)) & uint64_t(kSharcPosMask))));
    int level = int((key >> kSharcLevelShift) & uint64_t(kSharcLevelMask));
    float voxelSize = sharcVoxelSize(uint(level), grid);
    float inverse = 1.0 / voxelSize;
    int3 cameraCell = int3(floor(grid.camera * inverse));
    int3 cameraCellPrev = int3(floor(cameraPrev * inverse));
    int distanceNow = sharcGridDistance2(cameraCell - cell);
    int distancePrev = sharcGridDistance2(cameraCellPrev - cell);
    if (distanceNow < distancePrev) {
        cell = int3(floor(float3(cell) / grid.logBase));
        level = min(level + 1, int(kSharcLevelMask));
    } else {
        cell = int3(floor(float3(cell) * grid.logBase));
        level = max(level - 1, 1);
    }
    uint64_t modified = (uint64_t(uint(cell.x) & kSharcPosMask) << (kSharcPosBits * 0u))
        | (uint64_t(uint(cell.y) & kSharcPosMask) << (kSharcPosBits * 1u))
        | (uint64_t(uint(cell.z) & kSharcPosMask) << (kSharcPosBits * 2u))
        | (uint64_t(uint(level) & kSharcLevelMask) << kSharcLevelShift);
    modified |= key & (uint64_t(kSharcNormalMask) << kSharcNormalShift);
    return modified;
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
    if (sampleNumPrev == 0.0) {
        uint searchEnd = min(entryIndex + 1u + kSharcProbeWindow, gSharc.capacity);
        [loop]
        for (uint probe = entryIndex + 1u; probe < searchEnd; ++probe) {
            if (gHash[probe] == key) {
                resolvedRad = sharcLoadRadiance(probe);
                sampleNumPrev = resolvedRad.w;
                sharcLoadMeta(probe, accumulatedFrames, staleFrames);
                accumulatedFrames += 1u;
                staleFrames = 0u;
                break;
            }
        }
    }
    float scale = 1.0 / max(gSharc.radianceScale * max(sampleNum, 1e-6), 1e-6);
    float3 radianceNow = float3(float(accumulated.x), float(accumulated.y), float(accumulated.z)) * scale;
    float3 radiancePrev = resolvedRad.xyz;
    uint accumulationLimit = uint(clamp(gSharc.accumulationFrameNum, kSharcAccumMin, kSharcAccumMax));
    if (accumulatedFrames > accumulationLimit) {
        float normalize = float(accumulationLimit) / float(accumulatedFrames);
        accumulatedFrames = accumulationLimit;
        sampleNumPrev *= normalize;
    }
    float accumulatedSamples = sampleNumPrev + sampleNum;
    float3 blended = radianceNow;
    if (sampleNumPrev > 0.0) {
        blended = lerp(radiancePrev, radianceNow, 0.08);
    }
    SharcGrid grid;
    grid.camera = float3(gSharc.camX, gSharc.camY, gSharc.camZ);
    grid.logBase = kSharcLogBase;
    grid.sceneScale = gSharc.sceneScale;
    grid.levelBias = gSharc.levelBias;
    float3 cameraPrev = float3(gSharc.prevX, gSharc.prevY, gSharc.prevZ);
    float3 cameraDelta = grid.camera - cameraPrev;
    if (dot(cameraDelta, cameraDelta) > 1e-6 && accumulatedFrames <= 2u) {
        uint64_t adjacent = sharcAdjacentKey(key, grid, cameraPrev);
        uint adjacentIndex = kSharcInvalid;
        uint adjacentOffset = 0u;
        if (sharcFind(adjacent, sharcBaseSlot(adjacent, gSharc.capacity), kSharcBucket, adjacentIndex, adjacentOffset)) {
            float4 adjacentRad = sharcLoadRadiance(adjacentIndex);
            if (adjacentRad.w > kSharcSampleGate) {
                float blend = 1.0 / (adjacentRad.w + accumulatedSamples);
                blended = adjacentRad.xyz * (adjacentRad.w * blend) + blended * (accumulatedSamples * blend);
                accumulatedSamples += adjacentRad.w;
            }
        }
    }
    sharcStoreResolved(entryIndex, blended, accumulatedSamples, accumulatedFrames, staleFrames);
    sharcStoreAcc(entryIndex, uint4(0, 0, 0, 0));
}

SharcGrid sharcGrid() {
    SharcGrid grid;
    grid.camera = float3(gSharc.camX, gSharc.camY, gSharc.camZ);
    grid.logBase = kSharcLogBase;
    grid.sceneScale = gSharc.sceneScale;
    grid.levelBias = gSharc.levelBias;
    return grid;
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
