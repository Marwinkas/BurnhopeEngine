// SHaRC 1.8.3. Порт RTXGI Libraries/Sharc (HashGridCommon.h, SharcCommon.h, SharcTypes.h)
// при дефайнах сэмпла Pathtracer: 64-битный ключ, 64-битный CAS, нормали в ключе,
// без SH, без responsive lighting, separate emissive выключен, material demodulation включена,
// resampling на update, propagation depth 2, blend соседних уровней, fp16 через упаковку half.
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
    float3 delta = grid.camera - position;
    float distance2 = max(dot(delta, delta), 1e-10);
    float level = 0.5 * log2(distance2) / log2(grid.logBase) + grid.levelBias;
    return uint(clamp(level, 1.0, float(kSharcLevelMask)));
}

float sharcVoxelSize(uint level, SharcGrid grid) {
    float exponent = log2(grid.logBase) * (float(level) - grid.levelBias);
    return exp2(exponent) / grid.sceneScale;
}

int sharcSignPos(int packed) {
    return (packed << (32 - int(kSharcPosBits))) >> (32 - int(kSharcPosBits));
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
    uint normalBits = (normal.x + kSharcNormalBias >= 0.0 ? 0u : 1u)
        + (normal.y + kSharcNormalBias >= 0.0 ? 0u : 2u)
        + (normal.z + kSharcNormalBias >= 0.0 ? 0u : 4u);
    key |= uint64_t(normalBits) << kSharcNormalShift;
    return key;
}

void sharcCas(uint slot, uint64_t compareValue, uint64_t value, out uint64_t original) {
    InterlockedCompareExchange(gHash[slot], compareValue, value, original);
}

bool sharcInsert(uint64_t key, uint baseSlot, uint probeRange, out uint cacheIndex) {
    cacheIndex = kSharcInvalid;
    probeRange = min(probeRange, kSharcBucket);
    [loop]
    for (uint offset = 0u; offset < probeRange; ++offset) {
        uint64_t previous = 0;
        sharcCas(baseSlot + offset, uint64_t(0), key, previous);
        if (previous == uint64_t(0) || previous == key) {
            cacheIndex = baseSlot + offset;
            return true;
        }
    }
    return false;
}

bool sharcFind(uint64_t key, uint baseSlot, uint probeRange, out uint cacheIndex, out uint bucketOffset) {
    cacheIndex = kSharcInvalid;
    bucketOffset = 0u;
    probeRange = min(probeRange, kSharcBucket);
    uint emptyEntries = 0u;
    [loop]
    for (bucketOffset = 0u; bucketOffset < probeRange; ++bucketOffset) {
        uint64_t stored = gHash[baseSlot + bucketOffset];
        if (stored == uint64_t(0)) {
            if (emptyEntries > kSharcEmptyLimit) {
                break;
            }
            emptyEntries += 1u;
        }
        if (stored == key) {
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

uint sharcF32ToF16(float value) {
    uint bits = asuint(value);
    uint sign = (bits >> 16) & 0x8000u;
    int exponent = int((bits >> 23) & 0xffu) - 127 + 15;
    uint mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        return sign;
    }
    if (exponent >= 31) {
        return sign | 0x7c00u;
    }
    return sign | (uint(exponent) << 10) | (mantissa >> 13);
}

float sharcF16ToF32(uint encoded) {
    uint bits = encoded & 0xffffu;
    uint sign = (bits & 0x8000u) << 16;
    uint exponent = (bits >> 10) & 0x1fu;
    uint mantissa = bits & 0x3ffu;
    if (exponent == 0u) {
        return asfloat(sign);
    }
    if (exponent == 31u) {
        return asfloat(sign | 0x7f800000u);
    }
    return asfloat(sign | ((exponent + 112u) << 23) | (mantissa << 13));
}

uint sharcPackPair(float a, float b) {
    return sharcF32ToF16(a) | (sharcF32ToF16(b) << 16);
}

float sharcLuma(float3 color) {
    return dot(color, float3(0.213, 0.715, 0.072));
}

float4 sharcUnpackRadiance(uint4 packed) {
    return float4(
        sharcF16ToF32(packed.x),
        sharcF16ToF32(packed.x >> 16),
        sharcF16ToF32(packed.y),
        sharcF16ToF32(packed.y >> 16));
}

void sharcAddVoxel(uint cacheIndex, float3 sampleValue, float3 sampleWeight, uint sampleData) {
    uint3 scaled = uint3(max(sampleValue * sampleWeight * gSharc.radianceScale, float3(0.0, 0.0, 0.0)));
    uint ignored = 0u;
    if (scaled.x != 0u) {
        InterlockedAdd(gAcc[cacheIndex].x, scaled.x, ignored);
    }
    if (scaled.y != 0u) {
        InterlockedAdd(gAcc[cacheIndex].y, scaled.y, ignored);
    }
    if (scaled.z != 0u) {
        InterlockedAdd(gAcc[cacheIndex].z, scaled.z, ignored);
    }
    if (sampleData != 0u) {
        InterlockedAdd(gAcc[cacheIndex].w, sampleData, ignored);
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
        float4 stored = sharcUnpackRadiance(gResolved[cacheIndex]);
        if (stored.w > kSharcSampleGate) {
            radiance = max(stored.xyz, 0.0) * demod;
            keepTracing = false;
        }
    }
    if (keepTracing) {
        sharcAddVoxel(cacheIndex, directLighting * inverseDemod, float3(1.0, 1.0, 1.0), 1u);
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

bool sharcGetCachedRadiance(SharcHitData hit, SharcGrid grid, out float3 radiance) {
    radiance = 0.0;
    float voxelSize = 0.0;
    uint64_t key = sharcSpatialKey(hit.positionWorld, hit.normalWorld, grid, voxelSize);
    uint cacheIndex = kSharcInvalid;
    uint bucketOffset = 0u;
    if (!sharcFind(key, sharcBaseSlot(key, gSharc.capacity), kSharcBucket, cacheIndex, bucketOffset)) {
        return false;
    }
    float4 stored = sharcUnpackRadiance(gResolved[cacheIndex]);
    if (stored.w <= kSharcSampleGate) {
        return false;
    }
    radiance = max(stored.xyz, 0.0) * hit.materialDemodulation;
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

uint4 sharcPackVoxel(float3 radiance, float sampleNum, uint accumulatedFrames, uint staleFrames) {
    const float halfMax = 65504.0;
    float4 clamped = min(float4(max(radiance, 0.0), sampleNum), float4(halfMax, halfMax, halfMax, halfMax));
    uint4 packed;
    packed.x = sharcPackPair(clamped.x, clamped.y);
    packed.y = sharcPackPair(clamped.z, clamped.w);
    packed.z = accumulatedFrames | (staleFrames << 16);
    packed.w = 0u;
    return packed;
}

void sharcResolveEntry(uint entryIndex) {
    if (entryIndex >= gSharc.capacity) {
        return;
    }
    uint64_t key = gHash[entryIndex];
    if (key == uint64_t(0)) {
        return;
    }
    uint4 accumulated = gAcc[entryIndex];
    uint4 resolved = gResolved[entryIndex];
    float4 resolvedRad = sharcUnpackRadiance(resolved);
    float sampleNum = float(accumulated.w);
    float sampleNumPrev = resolvedRad.w;
    uint accumulatedFrames = (resolved.z & 0xffffu) + 1u;
    uint staleFrames = resolved.z >> 16;
    staleFrames = sampleNum != 0.0 ? 0u : staleFrames + 1u;
    uint staleLimit = uint(clamp(gSharc.staleFrameNum, kSharcStaleMin, kSharcStaleMax));
    if (staleFrames >= staleLimit) {
        gHash[entryIndex] = uint64_t(0);
        gAcc[entryIndex] = uint4(0, 0, 0, 0);
        gResolved[entryIndex] = uint4(0, 0, 0, 0);
        return;
    }
    if (sampleNum == 0.0) {
        gResolved[entryIndex].z = resolved.z + (1u | (1u << 16));
        return;
    }
    if (sampleNumPrev == 0.0) {
        uint searchEnd = min(entryIndex + 1u + kSharcProbeWindow, gSharc.capacity);
        [loop]
        for (uint probe = entryIndex + 1u; probe < searchEnd; ++probe) {
            if (gHash[probe] == key) {
                resolved = gResolved[probe];
                resolvedRad = sharcUnpackRadiance(resolved);
                sampleNumPrev = resolvedRad.w;
                accumulatedFrames = (resolved.z & 0xffffu) + 1u;
                staleFrames = 0u;
                break;
            }
        }
    }
    float scale = 1.0 / max(gSharc.radianceScale * max(sampleNum, 1e-6), 1e-6);
    float3 radianceNow = float3(accumulated.xyz) * scale;
    float3 radiancePrev = resolvedRad.xyz;
    uint accumulationLimit = uint(clamp(gSharc.accumulationFrameNum, kSharcAccumMin, kSharcAccumMax));
    if (accumulatedFrames > accumulationLimit) {
        float normalize = float(accumulationLimit) / float(accumulatedFrames);
        accumulatedFrames = accumulationLimit;
        sampleNumPrev *= normalize;
    }
    float sampleTotalInv = 1.0 / (sampleNumPrev + sampleNum);
    float3 blended = radiancePrev * (sampleNumPrev * sampleTotalInv) + radianceNow * (sampleNum * sampleTotalInv);
    float accumulatedSamples = sampleNumPrev + sampleNum;
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
            float4 adjacentRad = sharcUnpackRadiance(gResolved[adjacentIndex]);
            if (adjacentRad.w > kSharcSampleGate) {
                float blend = 1.0 / (adjacentRad.w + accumulatedSamples);
                blended = adjacentRad.xyz * (adjacentRad.w * blend) + blended * (accumulatedSamples * blend);
                accumulatedSamples += adjacentRad.w;
            }
        }
    }
    gResolved[entryIndex] = sharcPackVoxel(blended, accumulatedSamples, accumulatedFrames, staleFrames);
    gAcc[entryIndex] = uint4(0, 0, 0, 0);
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
