// Split Radiance Cascades. Хеш: uint64_t ключ пробы -> индекс. Направления лежат плотно.

[[vk::binding(30, 0)]] RWByteAddressBuffer gRc;

uint rcLoad(uint byteOff) {
    return gRc.Load(byteOff);
}

void rcStore(uint byteOff, uint value) {
    gRc.Store(byteOff, value);
}

uint hashCap() {
    return max(rcLoad(8), 1u);
}

uint hashBase(uint cascade) {
    return rcLoad(24 + cascade * 4u);
}

uint metaBase(uint cascade) {
    return rcLoad(40 + cascade * 4u);
}

uint dirBase(uint cascade) {
    return rcLoad(56 + cascade * 4u);
}

uint irrBase() {
    return rcLoad(72);
}

uint probeCap(uint cascade) {
    return rcLoad(76 + cascade * 4u);
}

uint countByte(uint cascade) {
    return 92u + cascade * 4u;
}

uint dirCountOf(uint cascade) {
    if (cascade == 0u) return 32u;
    if (cascade == 1u) return 128u;
    if (cascade == 2u) return 512u;
    return 2048u;
}

float rcParam(uint word, float fallback) {
    if (asfloat(rcLoad(128u)) <= 0.0) {
        return fallback;
    }
    return asfloat(rcLoad(128u + word * 4u));
}

float spacingAt(uint cascade, uint lod) {
    return max(gRcParams.spacing, 0.05) * exp2(float(cascade + lod));
}

int3 nearestCell(float3 world, uint cascade, uint lod) {
    return int3(floor(world / spacingAt(cascade, lod) - 0.5));
}

uint64_t packKey(int3 cell, uint lod) {
    const int bias = 131072;
    const uint64_t x = uint64_t(uint(cell.x + bias) & 0x3ffffu);
    const uint64_t y = uint64_t(uint(cell.y + bias) & 0x3ffffu);
    const uint64_t z = uint64_t(uint(cell.z + bias) & 0x3ffffu);
    const uint64_t L = uint64_t(lod & 0xffu);
    uint64_t key = x | (y << 18) | (z << 36) | (L << 54) | (uint64_t(1) << 63);
    if (uint(key) == 0u) {
        key |= uint64_t(1);
    }
    return key;
}

uint lookupProbe(uint cascade, uint64_t key) {
    const uint keyLo = uint(key);
    const uint keyHi = uint(key >> 32);
    const uint mask = hashCap() - 1u;
    const uint base = hashBase(cascade);
    const uint h = (keyLo ^ keyHi) & mask;
    [loop]
    for (uint i = 0u; i < 16u; ++i) {
        const uint off = base + ((h + i) & mask) * 16u;
        const uint lo = rcLoad(off);
        if (lo == 0u) {
            return 0xFFFFFFFFu;
        }
        if (lo == keyLo && rcLoad(off + 4u) == keyHi) {
            const uint tagged = rcLoad(off + 8u);
            if (tagged == 0u || tagged == 0xFFFFFFFFu) {
                return 0xFFFFFFFFu;
            }
            return tagged - 1u;
        }
    }
    return 0xFFFFFFFFu;
}

uint insertProbe(uint cascade, uint64_t key, int3 cell, uint lod) {
    const uint keyLo = uint(key);
    const uint keyHi = uint(key >> 32);
    const uint mask = hashCap() - 1u;
    const uint base = hashBase(cascade);
    const uint h = (keyLo ^ keyHi) & mask;
    [loop]
    for (uint i = 0u; i < 16u; ++i) {
        const uint off = base + ((h + i) & mask) * 16u;
        uint prev = 0u;
        gRc.InterlockedCompareExchange(off, 0u, keyLo, prev);
        if (prev != 0u && prev != keyLo) {
            continue;
        }
        if (prev == 0u) {
            rcStore(off + 4u, keyHi);
            uint idx = 0u;
            gRc.InterlockedAdd(countByte(cascade), 1u, idx);
            if (idx >= probeCap(cascade)) {
                rcStore(off + 8u, 0xFFFFFFFFu);
                return 0xFFFFFFFFu;
            }
            const uint meta = metaBase(cascade) + idx * 32u;
            rcStore(meta, asuint(cell.x));
            rcStore(meta + 4u, asuint(cell.y));
            rcStore(meta + 8u, asuint(cell.z));
            rcStore(meta + 12u, lod);
            rcStore(off + 8u, idx + 1u);
            return idx;
        }
        uint tagged = 0u;
        [loop]
        for (uint s = 0u; s < 8u && tagged == 0u; ++s) {
            tagged = rcLoad(off + 8u);
        }
        if (rcLoad(off + 4u) == keyHi && tagged != 0u && tagged != 0xFFFFFFFFu) {
            return tagged - 1u;
        }
    }
    return 0xFFFFFFFFu;
}

uint dirByte(uint cascade, uint probe, uint dir) {
    return dirBase(cascade) + (probe * dirCountOf(cascade) + dir) * 16u;
}

void depositDir(uint cascade, uint probe, uint dir, float3 rad, float beta) {
    if (probe == 0xFFFFFFFFu || dir >= dirCountOf(cascade)) {
        return;
    }
    const uint off = dirByte(cascade, probe, dir);
    gRc.InterlockedAdd(off, uint(min(rad.r, 32.0) * 8.0));
    gRc.InterlockedAdd(off + 4u, uint(min(rad.g, 32.0) * 8.0));
    gRc.InterlockedAdd(off + 8u, uint(min(rad.b, 32.0) * 8.0));
    const uint packed = (beta > 0.5 ? 1u : 0u) + (1u << 16);
    gRc.InterlockedAdd(off + 12u, packed);
}

float3 loadInterval(uint cascade, uint probe, uint dir, out float count, out float beta) {
    count = 0.0;
    beta = 1.0;
    if (probe == 0xFFFFFFFFu || dir >= dirCountOf(cascade)) {
        return 0.0;
    }
    const uint off = dirByte(cascade, probe, dir);
    const uint packed = rcLoad(off + 12u);
    count = float(packed >> 16);
    if (count < 1.0) {
        return 0.0;
    }
    beta = float(packed & 0xffffu) / count;
    return float3(rcLoad(off), rcLoad(off + 4u), rcLoad(off + 8u)) / count * (1.0 / 8.0);
}

void storeMerged(uint cascade, uint probe, uint dir, float3 value) {
    const uint off = dirByte(cascade, probe, dir);
    rcStore(off, asuint(value.x));
    rcStore(off + 4u, asuint(value.y));
    rcStore(off + 8u, asuint(value.z));
}

float3 loadMerged(uint cascade, uint probe, uint dir, out float count) {
    count = 0.0;
    if (probe == 0xFFFFFFFFu || dir >= dirCountOf(cascade)) {
        return 0.0;
    }
    const uint off = dirByte(cascade, probe, dir);
    count = float(rcLoad(off + 12u) >> 16);
    if (count < 1.0) {
        return 0.0;
    }
    return float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
}

float2 encodeDir(float3 w) {
    const float phi = atan2(w.y, w.x);
    return float2(frac(phi / 6.2831853), (w.z + 1.0) * 0.5);
}

float3 decodeDir(float2 uv) {
    const float phi = uv.x * 6.2831853;
    const float z = uv.y * 2.0 - 1.0;
    const float r = sqrt(max(1.0 - z * z, 0.0));
    return float3(r * cos(phi), r * sin(phi), z);
}

uint dirIndex(float3 w, uint cascade) {
    const float2 uv = encodeDir(w);
    const uint theta = 4u << cascade;
    const uint nu = theta * 2u;
    const uint u = min(uint(uv.x * float(nu)), nu - 1u);
    const uint v = min(uint(uv.y * float(theta)), theta - 1u);
    return v * nu + u;
}

float3 decodeGrid(uint cascade, uint dir) {
    const uint theta = 4u << cascade;
    const uint nu = theta * 2u;
    const uint u = dir % nu;
    const uint v = dir / nu;
    return decodeDir((float2(u, v) + 0.5) / float2(float(nu), float(theta)));
}
