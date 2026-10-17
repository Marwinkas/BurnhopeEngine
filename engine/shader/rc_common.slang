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

uint rcTriCount() {
    return rcLoad(168u);
}

float rcBaseSpacing() {
    return max(gPush.baseSpacing, 0.05);
}

// Одна мировая сетка. Камера в LOD не входит.
// 0..5 по главной оси нормали: пол и стены не перезаписывают друг друга
uint rcNormalFace(float3 N) {
    const float3 a = abs(N);
    if (a.x >= a.y && a.x >= a.z) {
        return N.x >= 0.0 ? 0u : 1u;
    }
    if (a.y >= a.z) {
        return N.y >= 0.0 ? 2u : 3u;
    }
    return N.z >= 0.0 ? 4u : 5u;
}

// Упаковка (Chebyshev LOD 0..5) и (Normal Face 0..5) в 9-битное поле lod ключа
uint rcMakeTag(uint lod, uint face) {
    return (lod & 7u) | ((face & 7u) << 3u);
}

uint rcTagLod(uint tag) {
    return tag & 7u;
}

// Расстояние Чебышёва от камеры (Секция 4.1 статьи, стр. 7)
float rcChebDist(float3 P, float3 camPos) {
    const float3 d = abs(P - camPos);
    return max(d.x, max(d.y, d.z));
}

float rcLodBaseRadius() {
    return max(rcBaseSpacing() * 48.0, 18.0);
}

uint rcLod(float3 P) {
    return 0u;
}

float rcSpacing(uint cascade, uint tag) {
    const uint lod = rcTagLod(tag);
    return rcBaseSpacing() * exp2(float(cascade + lod));
}

int3 rcCell(float3 world, uint cascade, uint tag) {
    return int3(floor(world / max(rcSpacing(cascade, tag), 1e-3)));
}

float3 rcCenter(int3 cell, uint cascade, uint tag) {
    return (float3(cell) + 0.5) * rcSpacing(cascade, tag);
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

float rcT0(uint tag) {
    const uint lod = rcTagLod(tag);
    const float reachScale = clamp(gPush.baseLength / 48.0, 0.1, 8.0);
    return 1.6 * rcSpacing(0u, lod) * reachScale;
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
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31;
    const uint lo = uint(key);
    if (lo == 0u || lo == 0xFFFFFFFFu) {
        key ^= 1ULL;
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

// Lockless вставка без спин-лока: возвращает true только тому потоку, который первым создал зонд!
bool rcEnsureProbe(uint cascade, int3 cell, uint tag, float3 P, float3 N) {
    const uint64_t key = rcPackKey(cell, tag);
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
        const uint cur = rcLoad(off);
        if (cur == keyLo) {
            return false;
        }
        if (cur != 0u) {
            continue;
        }
        uint prev = 0u;
        gRc.InterlockedCompareExchange(off, 0u, keyLo, prev);
        if (prev == keyLo) {
            return false;
        }
        if (prev != 0u) {
            continue;
        }
        rcStore(off + 4u, keyHi);
        uint idx = 0u;
        gRc.InterlockedAdd(rcCountAddr(cascade), 1u, idx);
        if (idx < cap) {
            const uint meta = rcMetaAddr(cascade, idx);
            rcStore(meta, asuint(cell.x));
            rcStore(meta + 4u, asuint(cell.y));
            rcStore(meta + 8u, asuint(cell.z));
            rcStore(meta + 12u, tag);
            rcStore(meta + 16u, kInvalid);
            rcStore(meta + 20u, 32u);
            rcStore(meta + 24u, 0u);
            rcStore(meta + 28u, 0u);
            rcStore(meta + 32u, asuint(P.x));
            rcStore(meta + 36u, asuint(P.y));
            rcStore(meta + 40u, asuint(P.z));
            rcStore(meta + 44u, asuint(N.x));
            rcStore(meta + 48u, asuint(N.y));
            rcStore(meta + 52u, asuint(N.z));
            rcStore(off + 8u, idx + 1u);
            return true;
        } else {
            rcStore(off + 8u, kOverCap);
            if (cascade == 0u) {
                uint ignored = 0u;
                gRc.InterlockedAdd(236u, 1u, ignored);
            }
            return false;
        }
    }
    return false;
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

// Находит родительский зонд в каскаде cascade + 1 напрямую по хеш-таблице после барьера
uint rcParent(uint cascade, uint probe) {
    if (probe == kInvalid || cascade >= 3u || probe >= rcProbeCap(cascade)) {
        return kInvalid;
    }
    const uint meta = rcMetaAddr(cascade, probe);
    const int3 cell = int3(asint(rcLoad(meta)), asint(rcLoad(meta + 4u)), asint(rcLoad(meta + 8u)));
    const uint tag = rcLoad(meta + 12u);
    const float3 at = rcCenter(cell, cascade, tag);
    const int3 up = rcCell(at, cascade + 1u, tag);
    return rcLookup(cascade + 1u, rcPackKey(up, tag));
}