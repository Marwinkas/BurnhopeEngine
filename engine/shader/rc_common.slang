// Split Radiance Cascades. Глобальный заголовок буфера — 256 байт, его пишет хост.
// Дальше два среза кадра (flight в бите 0 push). Смещения среза лежат в заголовке.
// Слот кучи — HeapBuf::Rc, binding 30.

struct RcPush {
    uint cascadeIndex;
    float baseSpacing;
    float baseLength;
    uint frameIndex;
};

[[vk::push_constant]]
RcPush gPush;

[[vk::binding(30, 0)]]
globallycoherent RWByteAddressBuffer gRc;

static const uint kInvalid = 0xFFFFFFFFu;
static const uint kOverCap = 0xFFFFFFFEu;
static const uint kLock = 0xFFFFFFFFu;
static const uint kSliceOrigin = 256u;
static const uint kRadScale = 256u;
static const float kRadMax = 64.0;
static const uint kHashWalk = 64u;

uint rcLoad(uint byteOff) {
    return gRc.Load(byteOff);
}

void rcStore(uint byteOff, uint value) {
    gRc.Store(byteOff, value);
}

uint rcSliceStride() {
    return rcLoad(0u);
}

uint sliceBase() {
    return kSliceOrigin + (gPush.frameIndex & 1u) * rcSliceStride();
}

uint rcHashCap() {
    return max(rcLoad(8u), 1u);
}

uint rcPosByte() {
    return rcLoad(12u);
}

uint rcHashRel(uint cascade) {
    return rcLoad(16u + cascade * 4u);
}

uint rcMetaRel(uint cascade) {
    return rcLoad(32u + cascade * 4u);
}

uint rcDirRel(uint cascade) {
    return rcLoad(48u + cascade * 4u);
}

uint rcMergedRel(uint cascade) {
    return rcLoad(64u + cascade * 4u);
}

uint rcIrrRel() {
    return rcLoad(80u);
}

uint rcPrefixRel() {
    return rcLoad(84u);
}

uint rcClaimRel() {
    return rcLoad(88u);
}

uint rcProbeCap(uint cascade) {
    return rcLoad(92u + cascade * 4u);
}

uint rcHashStride() {
    return rcLoad(120u);
}

uint rcMetaStride() {
    return rcLoad(124u);
}

uint rcDirStride() {
    return rcLoad(128u);
}

uint rcMergedStride() {
    return rcLoad(132u);
}

uint rcIrrStride() {
    return rcLoad(136u);
}

uint rcDirCount(uint cascade) {
    return rcLoad(140u + cascade * 4u);
}

uint rcCountAddr(uint cascade) {
    return sliceBase() + cascade * 4u;
}

uint rcCount(uint cascade) {
    return rcLoad(rcCountAddr(cascade));
}

uint rcMetaAddr(uint cascade, uint probe) {
    return sliceBase() + rcMetaRel(cascade) + probe * rcMetaStride();
}

uint rcDirAddr(uint cascade, uint probe, uint dir) {
    return sliceBase() + rcDirRel(cascade) + (probe * rcDirCount(cascade) + dir) * rcDirStride();
}

uint rcMergedAddr(uint cascade, uint probe, uint dir) {
    return sliceBase() + rcMergedRel(cascade) + (probe * rcDirCount(cascade) + dir) * rcMergedStride();
}

uint rcPrefixAddr(uint probe) {
    return sliceBase() + rcPrefixRel() + probe * 4u;
}

uint rcClaimAddr(uint probe) {
    return sliceBase() + rcClaimRel() + probe * 4u;
}

uint rcScanBlocks() {
    return max(rcLoad(108u), 1u);
}

uint rcBlockSumAddr(uint block) {
    return sliceBase() + rcLoad(156u) + block * 4u;
}

uint rcBlockExcAddr(uint block) {
    return sliceBase() + rcLoad(160u) + block * 4u;
}

uint rcArgsAddr(uint slot) {
    return sliceBase() + rcLoad(164u) + slot * 16u;
}

// Смешивает все биты ключа. Младшие биты X отдельно склеивают колонку стены в одну цепочку.
uint rcSlot(uint keyLo, uint keyHi, uint mask) {
    uint h = keyLo ^ (keyHi * 0x9E3779B1u);
    h ^= h >> 16u;
    h *= 0x7FEB352Du;
    h ^= h >> 15u;
    h *= 0x846CA68Bu;
    h ^= h >> 16u;
    return h & mask;
}

float rcT0(uint lod) {
    return gPush.baseLength * exp2(float(lod));
}

// 18+18+18 бит клетки и 9 бит LOD. Бит 63 держит keyHi ненулевым.
// Младшее слово не бывает 0 и не бывает 0xFFFFFFFF: это пустой слот и замок вставки.
uint64_t rcPackKey(int3 cell, uint lod) {
    const int bias = 131072;
    const uint64_t x = uint64_t(uint(cell.x + bias) & 0x3ffffu);
    const uint64_t y = uint64_t(uint(cell.y + bias) & 0x3ffffu);
    const uint64_t z = uint64_t(uint(cell.z + bias) & 0x3ffffu);
    const uint64_t L = uint64_t(lod & 0x1ffu);
    uint64_t key = x | (y << 18) | (z << 36) | (L << 54) | (uint64_t(1) << 63);
    const uint lo = uint(key);
    if (lo == 0u || lo == 0xFFFFFFFFu) {
        key ^= 1u;
    }
    return key;
}

// Равновеликая цилиндрическая проекция. Площадь равномерна по (азимут, z).
float2 rcEncodeDir(float3 w) {
    const float phi = atan2(w.y, w.x);
    return float2(frac(phi / 6.28318530718), (w.z + 1.0) * 0.5);
}

float3 rcDecodeDir(float2 uv) {
    const float phi = uv.x * 6.28318530718;
    const float z = uv.y * 2.0 - 1.0;
    const float r = sqrt(max(1.0 - z * z, 0.0));
    return float3(r * cos(phi), r * sin(phi), z);
}

// Morton 8×4 на грубом уровне. Младшие биты — квадрант более тонкого каскада,
// поэтому четыре ребёнка направления d лежат в d*4+0..3.
uint rcMortonPack(uint x, uint y) {
    uint code = 0u;
    code |= (x & 1u) << 0u;
    code |= (y & 1u) << 1u;
    code |= (x & 2u) << 1u;
    code |= (y & 2u) << 2u;
    code |= (x & 4u) << 2u;
    return code;
}

void rcMortonUnpack(uint code, out uint x, out uint y) {
    x = (code & 1u) | ((code & 4u) >> 1u) | ((code & 16u) >> 2u);
    y = ((code & 2u) >> 1u) | ((code & 8u) >> 2u);
}

uint rcHierIndex(uint u, uint v, uint cascade) {
    const uint coarse = rcMortonPack(u >> cascade, v >> cascade);
    uint fine = 0u;
    if (cascade > 0u) {
        fine |= ((v & 1u) << 1u) | (u & 1u);
    }
    if (cascade > 1u) {
        fine |= ((((v >> 1u) & 1u) << 1u) | ((u >> 1u) & 1u)) << 2u;
    }
    if (cascade > 2u) {
        fine |= ((((v >> 2u) & 1u) << 1u) | ((u >> 2u) & 1u)) << 4u;
    }
    return (coarse << (cascade * 2u)) | fine;
}

void rcHierDecode(uint dir, uint cascade, out uint u, out uint v) {
    uint cu;
    uint cv;
    rcMortonUnpack(dir >> (cascade * 2u), cu, cv);
    u = cu << cascade;
    v = cv << cascade;
    if (cascade > 0u) {
        const uint q = dir & 3u;
        u |= q & 1u;
        v |= q >> 1u;
    }
    if (cascade > 1u) {
        const uint q = (dir >> 2u) & 3u;
        u |= (q & 1u) << 1u;
        v |= (q >> 1u) << 1u;
    }
    if (cascade > 2u) {
        const uint q = (dir >> 4u) & 3u;
        u |= (q & 1u) << 2u;
        v |= (q >> 1u) << 2u;
    }
}

uint rcDirIndex(float3 w, uint cascade) {
    const float2 uv = rcEncodeDir(w);
    const uint theta = 4u << cascade;
    const uint nu = theta * 2u;
    const uint u = min(uint(uv.x * float(nu)), nu - 1u);
    const uint v = min(uint(uv.y * float(theta)), theta - 1u);
    return rcHierIndex(u, v, cascade);
}

float3 rcDecodeGrid(uint cascade, uint dir) {
    uint u;
    uint v;
    rcHierDecode(dir, cascade, u, v);
    const uint theta = 4u << cascade;
    const uint nu = theta * 2u;
    return rcDecodeDir((float2(u, v) + 0.5) / float2(float(nu), float(theta)));
}

// R2, пластическая константа. Низкая дискрепансность на отрезке, который выдаёт префиксная сумма.
float2 rcR2(uint n) {
    return frac(float2(0.7548776662466927, 0.5698402909980532) * float(n) + 0.5);
}

uint rcLookup(uint cascade, uint64_t key) {
    const uint keyLo = uint(key);
    const uint keyHi = uint(key >> 32);
    const uint mask = rcHashCap() - 1u;
    const uint stride = rcHashStride();
    const uint base = sliceBase() + rcHashRel(cascade);
    const uint h = rcSlot(keyLo, keyHi, mask);
    [loop]
    for (uint i = 0u; i < kHashWalk; ++i) {
        const uint off = base + ((h + i) & mask) * stride;
        const uint lo = rcLoad(off);
        if (lo == 0u) {
            return kInvalid;
        }
        if (lo != keyLo || rcLoad(off + 4u) != keyHi) {
            continue;
        }
        const uint state = rcLoad(off + 8u);
        if (state == 0u || state >= kOverCap) {
            return kInvalid;
        }
        return state - 1u;
    }
    return kInvalid;
}

uint rcInsert(uint cascade, uint64_t key, int3 cell, uint lod) {
    const uint keyLo = uint(key);
    const uint keyHi = uint(key >> 32);
    const uint mask = rcHashCap() - 1u;
    const uint stride = rcHashStride();
    const uint base = sliceBase() + rcHashRel(cascade);
    const uint h = rcSlot(keyLo, keyHi, mask);
    const uint cap = rcProbeCap(cascade);
    [loop]
    for (uint i = 0u; i < kHashWalk; ++i) {
        const uint off = base + ((h + i) & mask) * stride;
        uint prev = 0u;
        gRc.InterlockedCompareExchange(off, 0u, kLock, prev);
        if (prev == 0u) {
            uint ignored = 0u;
            gRc.InterlockedExchange(off + 4u, keyHi, ignored);
            uint idx = 0u;
            gRc.InterlockedAdd(rcCountAddr(cascade), 1u, idx);
            if (idx < cap) {
                const uint meta = rcMetaAddr(cascade, idx);
                rcStore(meta, asuint(cell.x));
                rcStore(meta + 4u, asuint(cell.y));
                rcStore(meta + 8u, asuint(cell.z));
                rcStore(meta + 12u, lod);
                rcStore(meta + 16u, kInvalid);
                rcStore(meta + 24u, 0u);
                rcStore(meta + 28u, 0xFFFFFFFFu);
                DeviceMemoryBarrier();
                gRc.InterlockedExchange(off + 8u, idx + 1u, ignored);
            } else {
                gRc.InterlockedExchange(off + 8u, kOverCap, ignored);
            }
            DeviceMemoryBarrier();
            gRc.InterlockedExchange(off, keyLo, ignored);
            return idx < cap ? idx : kInvalid;
        }
        uint seen = prev;
        [loop]
        for (uint s = 0u; s < 256u; ++s) {
            if (seen != 0u && seen != kLock) {
                break;
            }
            gRc.InterlockedCompareExchange(off, 0u, 0u, seen);
        }
        if (seen != keyLo) {
            continue;
        }
        uint hi = 0u;
        [loop]
        for (uint s = 0u; s < 64u; ++s) {
            gRc.InterlockedCompareExchange(off + 4u, 0u, 0u, hi);
            if (hi != 0u) {
                break;
            }
        }
        if (hi != keyHi) {
            continue;
        }
        uint state = 0u;
        [loop]
        for (uint s = 0u; s < 64u; ++s) {
            gRc.InterlockedCompareExchange(off + 8u, 0u, 0u, state);
            if (state != 0u) {
                break;
            }
        }
        if (state == 0u || state >= kOverCap) {
            return kInvalid;
        }
        return state - 1u;
    }
    return kInvalid;
}

void rcDeposit(uint cascade, uint probe, uint dir, float3 rad, float beta) {
    if (probe == kInvalid || dir >= rcDirCount(cascade) || probe >= rcProbeCap(cascade)) {
        return;
    }
    const uint off = rcDirAddr(cascade, probe, dir);
    uint ignored = 0u;
    gRc.InterlockedAdd(off, uint(clamp(rad.r, 0.0, kRadMax) * float(kRadScale) + 0.5), ignored);
    gRc.InterlockedAdd(off + 4u, uint(clamp(rad.g, 0.0, kRadMax) * float(kRadScale) + 0.5), ignored);
    gRc.InterlockedAdd(off + 8u, uint(clamp(rad.b, 0.0, kRadMax) * float(kRadScale) + 0.5), ignored);
    if (beta > 0.5) {
        gRc.InterlockedAdd(off + 12u, 1u, ignored);
    }
    gRc.InterlockedAdd(off + 16u, 1u, ignored);
}

void rcLoadAccum(uint cascade, uint probe, uint dir, out float3 J, out float beta, out float count) {
    J = 0.0;
    beta = 1.0;
    count = 0.0;
    if (probe == kInvalid || dir >= rcDirCount(cascade)) {
        return;
    }
    const uint off = rcDirAddr(cascade, probe, dir);
    count = float(rcLoad(off + 16u));
    if (count < 1.0) {
        return;
    }
    J = float3(rcLoad(off), rcLoad(off + 4u), rcLoad(off + 8u)) / (count * float(kRadScale));
    beta = float(rcLoad(off + 12u)) / count;
}

void rcStoreMerged(uint cascade, uint probe, uint dir, float3 value, float valid) {
    const uint off = rcMergedAddr(cascade, probe, dir);
    rcStore(off, asuint(value.x));
    rcStore(off + 4u, asuint(value.y));
    rcStore(off + 8u, asuint(value.z));
    rcStore(off + 12u, asuint(valid));
}

void rcLoadMerged(uint cascade, uint probe, uint dir, out float3 value, out float valid) {
    value = 0.0;
    valid = 0.0;
    if (probe == kInvalid || dir >= rcDirCount(cascade) || probe >= rcProbeCap(cascade)) {
        return;
    }
    const uint off = rcMergedAddr(cascade, probe, dir);
    value = float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
    valid = asfloat(rcLoad(off + 12u));
}

uint rcParent(uint cascade, uint probe) {
    if (probe == kInvalid || probe >= rcProbeCap(cascade)) {
        return kInvalid;
    }
    return rcLoad(rcMetaAddr(cascade, probe) + 16u);
}
