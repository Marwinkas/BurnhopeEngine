// GPU-компьют BC1 (план рендера, Фаза 2). Один поток на блок 4x4, без alpha (только непрозрачный
// путь — тот же случай, что CPU ISPC BC1 в ImageFile.cpp). Источник и назначение — свои буферы,
// не трогают heap UI (gUi) и не второй рендер: отдельный одноразовый compute-проход, который
// core/image/ImageFile.cpp вызывает и сразу читает результат обратно через staging-буфер.
//
// Источник: [uint width, uint height, pad, pad] затем RGBA8 пиксели с offset 16, ряд за рядом.
// Назначение: блоки BC1 по 8 байт (color0 u16, color1 u16, indices u32), blocksX*blocksY штук.

[[vk::binding(0, 0)]]
ByteAddressBuffer gSrc;
[[vk::binding(1, 0)]]
RWByteAddressBuffer gDst;

static const uint kSrcHeader = 16;

float3 loadTexel(uint width, uint height, uint x, uint y) {
    const uint cx = min(x, width - 1);
    const uint cy = min(y, height - 1);
    const uint word = gSrc.Load(kSrcHeader + (cy * width + cx) * 4);
    return float3(
        float(word & 0xffu),
        float((word >> 8) & 0xffu),
        float((word >> 16) & 0xffu));
}

uint pack565(float3 c) {
    const uint r = uint(clamp(c.x, 0.0, 255.0) / 255.0 * 31.0 + 0.5);
    const uint g = uint(clamp(c.y, 0.0, 255.0) / 255.0 * 63.0 + 0.5);
    const uint b = uint(clamp(c.z, 0.0, 255.0) / 255.0 * 31.0 + 0.5);
    return (r << 11) | (g << 5) | b;
}

float3 unpack565(uint v) {
    const float r = float((v >> 11) & 0x1fu) * (255.0 / 31.0);
    const float g = float((v >> 5) & 0x3fu) * (255.0 / 63.0);
    const float b = float(v & 0x1fu) * (255.0 / 31.0);
    return float3(r, g, b);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc1(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }

    float3 texel[16];
    float3 lo = float3(255.0, 255.0, 255.0);
    float3 hi = float3(0.0, 0.0, 0.0);
    for (uint ty = 0; ty < 4; ++ty) {
        for (uint tx = 0; tx < 4; ++tx) {
            const float3 c = loadTexel(width, height, tid.x * 4 + tx, tid.y * 4 + ty);
            texel[ty * 4 + tx] = c;
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }

    uint c0 = pack565(hi);
    uint c1 = pack565(lo);
    if (c0 <= c1) {
        if (c1 < 0xffffu) {
            c0 = c1 + 1u;
        } else {
            c1 = c0 - 1u;
        }
    }
    const float3 color0 = unpack565(c0);
    const float3 color1 = unpack565(c1);
    const float3 color2 = (color0 * 2.0 + color1) / 3.0;
    const float3 color3 = (color0 + color1 * 2.0) / 3.0;

    uint indices = 0;
    for (uint i = 0; i < 16; ++i) {
        const float3 c = texel[i];
        const float d0 = dot(c - color0, c - color0);
        const float d1 = dot(c - color1, c - color1);
        const float d2 = dot(c - color2, c - color2);
        const float d3 = dot(c - color3, c - color3);
        uint best = 0;
        float bestD = d0;
        if (d1 < bestD) { best = 1; bestD = d1; }
        if (d2 < bestD) { best = 2; bestD = d2; }
        if (d3 < bestD) { best = 3; bestD = d3; }
        // BC1 index order: 0=color0, 1=color1, 2=color2, 3=color3 — но физический код для
        // "2/3,1/3" и "1/3,2/3" в формате идёт как 2 и 3 с тем же порядком, что мы считали выше.
        indices |= best << (i * 2);
    }

    const uint blockIndex = tid.y * blocksX + tid.x;
    const uint outOff = blockIndex * 8u;
    gDst.Store(outOff, c0 | (c1 << 16));
    gDst.Store(outOff + 4u, indices);
}

// BC7 mode 6: один subset, 7 бит на канал + p-bit на endpoint, индекс 4 бита
// (у пикселя 0 старший бит не пишется — якорь). 128 бит ровно. Без партиций и без rotation.

float4 loadTexel4(uint width, uint height, uint x, uint y) {
    const uint cx = min(x, width - 1);
    const uint cy = min(y, height - 1);
    const uint word = gSrc.Load(kSrcHeader + (cy * width + cx) * 4);
    return float4(
        float(word & 0xffu),
        float((word >> 8) & 0xffu),
        float((word >> 16) & 0xffu),
        float((word >> 24) & 0xffu));
}

uint weight4(uint i) {
    const uint w[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    return w[i];
}

uint quant7(float v, uint p) {
    const float target = clamp(v, 0.0, 255.0);
    int base = int(floor(target + 0.5));
    if (base < 0) {
        base = 0;
    }
    if (base > 255) {
        base = 255;
    }
    if ((uint(base) & 1u) != p) {
        const int down = base > 0 ? base - 1 : base + 1;
        const int up = base < 255 ? base + 1 : base - 1;
        const float dDown = abs(target - float(down));
        const float dUp = abs(target - float(up));
        base = dDown <= dUp ? down : up;
    }
    return uint(base) >> 1;
}

float reconErr(float4 v, uint p) {
    float e = 0.0;
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        const float r = float((quant7(v[c], p) << 1) | p);
        const float d = r - v[c];
        e += d * d;
    }
    return e;
}

void writeBits(inout uint4 block, inout uint pos, uint value, uint n) {
    while (n > 0u) {
        const uint word = pos >> 5;
        const uint shift = pos & 31u;
        const uint room = 32u - shift;
        const uint take = min(room, n);
        const uint mask = take == 32u ? 0xffffffffu : ((1u << take) - 1u);
        const uint chunk = (value & mask) << shift;
        if (word == 0u) {
            block.x |= chunk;
        } else if (word == 1u) {
            block.y |= chunk;
        } else if (word == 2u) {
            block.z |= chunk;
        } else {
            block.w |= chunk;
        }
        value >>= take;
        pos += take;
        n -= take;
    }
}

int4 interpAt(int4 e0, int4 e1, uint index) {
    const int w = int(weight4(index));
    int4 rec;
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        rec[c] = ((64 - w) * e0[c] + w * e1[c] + 32) >> 6;
    }
    return rec;
}

uint bestIndex(int4 color, int4 e0, int4 e1, uint maxIndex) {
    uint best = 0;
    int bestD = 0x7fffffff;
    for (uint i = 0; i <= maxIndex; ++i) {
        const int4 rec = interpAt(e0, e1, i);
        int d = 0;
        [unroll]
        for (uint c = 0; c < 4; ++c) {
            const int diff = rec[c] - color[c];
            d += diff * diff;
        }
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc7(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }

    float4 texel[16];
    float4 lo = float4(255.0, 255.0, 255.0, 255.0);
    float4 hi = float4(0.0, 0.0, 0.0, 0.0);
    [unroll]
    for (uint ty = 0; ty < 4; ++ty) {
        [unroll]
        for (uint tx = 0; tx < 4; ++tx) {
            const float4 c = loadTexel4(width, height, tid.x * 4 + tx, tid.y * 4 + ty);
            texel[ty * 4 + tx] = c;
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }

    const uint p0 = reconErr(lo, 1u) < reconErr(lo, 0u) ? 1u : 0u;
    const uint p1 = reconErr(hi, 1u) < reconErr(hi, 0u) ? 1u : 0u;
    uint q0[4];
    uint q1[4];
    int4 e0;
    int4 e1;
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        q0[c] = quant7(lo[c], p0);
        q1[c] = quant7(hi[c], p1);
        e0[c] = int((q0[c] << 1) | p0);
        e1[c] = int((q1[c] << 1) | p1);
    }

    int4 pix[16];
    [unroll]
    for (uint i = 0; i < 16; ++i) {
        pix[i] = int4(int(texel[i].x + 0.5), int(texel[i].y + 0.5), int(texel[i].z + 0.5), int(texel[i].w + 0.5));
    }
    if (bestIndex(pix[0], e0, e1, 15u) >= 8u) {
        const int4 se = e0;
        e0 = e1;
        e1 = se;
    }
    const uint wp0 = uint(e0.x) & 1u;
    const uint wp1 = uint(e1.x) & 1u;
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        q0[c] = uint(e0[c]) >> 1;
        q1[c] = uint(e1[c]) >> 1;
    }

    uint indices[16];
    indices[0] = bestIndex(pix[0], e0, e1, 7u);
    for (uint i = 1; i < 16; ++i) {
        indices[i] = bestIndex(pix[i], e0, e1, 15u);
    }

    uint4 block = uint4(0, 0, 0, 0);
    uint pos = 0;
    writeBits(block, pos, 0x40u, 7u);
    [unroll]
    for (uint c = 0; c < 4; ++c) {
        writeBits(block, pos, q0[c], 7u);
        writeBits(block, pos, q1[c], 7u);
    }
    writeBits(block, pos, wp0, 1u);
    writeBits(block, pos, wp1, 1u);
    writeBits(block, pos, indices[0], 3u);
    for (uint i = 1; i < 16; ++i) {
        writeBits(block, pos, indices[i], 4u);
    }

    const uint blockIndex = tid.y * blocksX + tid.x;
    const uint outOff = blockIndex * 16u;
    gDst.Store(outOff, block.x);
    gDst.Store(outOff + 4u, block.y);
    gDst.Store(outOff + 8u, block.z);
    gDst.Store(outOff + 12u, block.w);
}

// BC6H mode 11 (биты режима 0b00011): один subset, оба конца явно по 10 бит на канал,
// без дельты и без партиций. Индекс 4 бита, якорь (0,0) — 3 бита. Unsigned.

float3 loadHdr(uint width, uint height, uint x, uint y) {
    const uint cx = min(x, width - 1);
    const uint cy = min(y, height - 1);
    const uint off = kSrcHeader + (cy * width + cx) * 12u;
    return float3(asfloat(gSrc.Load(off)), asfloat(gSrc.Load(off + 4u)), asfloat(gSrc.Load(off + 8u)));
}

uint floatToHalfBits(float value) {
    const uint bits = asuint(value);
    const int exp = int((bits >> 23) & 0xffu) - 127 + 15;
    const uint mant = bits & 0x7fffffu;
    if (exp <= 0) {
        return 0u;
    }
    if (exp >= 31) {
        return 0x7c00u;
    }
    return (uint(exp) << 10) | (mant >> 13);
}

int unq10(uint q) {
    if (q == 0u) {
        return 0;
    }
    if (q >= 1023u) {
        return 0xffff;
    }
    return int(((q << 16) + 0x8000u) >> 10);
}

int finishHalf(int unq) {
    return (unq * 31) >> 6;
}

uint quant10(float v) {
    const float c = clamp(v, 0.0, 65504.0);
    const uint h = floatToHalfBits(c);
    if (h <= 23u) {
        return 0u;
    }
    if (h >= 31743u) {
        return 1023u;
    }
    int q = int(floor((float(h) - 15.0) / 31.0 + 0.5));
    if (q < 1) {
        q = 1;
    }
    if (q > 1022) {
        q = 1022;
    }
    return uint(q);
}

int interpUnq(int a, int b, uint index) {
    const int w = int(weight4(index));
    return ((64 - w) * a + w * b + 32) >> 6;
}

uint bestHdr(uint3 halfPx, int3 e0, int3 e1, uint maxIndex) {
    uint best = 0;
    int bestD = 0x7fffffff;
    for (uint i = 0; i <= maxIndex; ++i) {
        int d = 0;
        [unroll]
        for (uint c = 0; c < 3; ++c) {
            const int rec = finishHalf(interpUnq(e0[c], e1[c], i));
            const int diff = rec - int(halfPx[c]);
            d += diff * diff;
        }
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc6h(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }

    float3 lo = float3(65504.0, 65504.0, 65504.0);
    float3 hi = float3(0.0, 0.0, 0.0);
    uint3 halfPx[16];
    [unroll]
    for (uint ty = 0; ty < 4; ++ty) {
        [unroll]
        for (uint tx = 0; tx < 4; ++tx) {
            const uint i = ty * 4u + tx;
            const float3 c = max(loadHdr(width, height, tid.x * 4u + tx, tid.y * 4u + ty), float3(0.0, 0.0, 0.0));
            lo = min(lo, c);
            hi = max(hi, c);
            halfPx[i] = uint3(floatToHalfBits(c.x), floatToHalfBits(c.y), floatToHalfBits(c.z));
        }
    }

    uint q0[3];
    uint q1[3];
    int3 e0;
    int3 e1;
    [unroll]
    for (uint c = 0; c < 3; ++c) {
        q0[c] = quant10(lo[c]);
        q1[c] = quant10(hi[c]);
        e0[c] = unq10(q0[c]);
        e1[c] = unq10(q1[c]);
    }
    if (bestHdr(halfPx[0], e0, e1, 15u) >= 8u) {
        const int3 se = e0;
        e0 = e1;
        e1 = se;
        [unroll]
        for (uint c = 0; c < 3; ++c) {
            const uint sq = q0[c];
            q0[c] = q1[c];
            q1[c] = sq;
        }
    }

    uint indices[16];
    indices[0] = bestHdr(halfPx[0], e0, e1, 7u);
    for (uint i = 1u; i < 16u; ++i) {
        indices[i] = bestHdr(halfPx[i], e0, e1, 15u);
    }

    uint4 block = uint4(0, 0, 0, 0);
    uint pos = 0;
    writeBits(block, pos, 0x03u, 5u);
    [unroll]
    for (uint c = 0; c < 3; ++c) {
        writeBits(block, pos, q0[c], 10u);
    }
    [unroll]
    for (uint c = 0; c < 3; ++c) {
        writeBits(block, pos, q1[c], 10u);
    }
    writeBits(block, pos, indices[0], 3u);
    for (uint i = 1u; i < 16u; ++i) {
        writeBits(block, pos, indices[i], 4u);
    }

    const uint blockIndex = tid.y * blocksX + tid.x;
    const uint outOff = blockIndex * 16u;
    gDst.Store(outOff, block.x);
    gDst.Store(outOff + 4u, block.y);
    gDst.Store(outOff + 8u, block.z);
    gDst.Store(outOff + 12u, block.w);
}

void storeBc4(uint outOff, uint channel, uint3 tid) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    float texel[16];
    float lo = 255.0;
    float hi = 0.0;
    [unroll]
    for (uint ty = 0; ty < 4; ++ty) {
        [unroll]
        for (uint tx = 0; tx < 4; ++tx) {
            const float4 c = loadTexel4(width, height, tid.x * 4u + tx, tid.y * 4u + ty);
            const float v = c[channel];
            texel[ty * 4u + tx] = v;
            lo = min(lo, v);
            hi = max(hi, v);
        }
    }
    uint e0 = uint(floor(hi + 0.5));
    uint e1 = uint(floor(lo + 0.5));
    if (e0 > 255u) {
        e0 = 255u;
    }
    if (e1 > 255u) {
        e1 = 255u;
    }
    if (e0 <= e1) {
        if (e1 < 255u) {
            e0 = e1 + 1u;
        } else if (e0 > 0u) {
            e1 = e0 - 1u;
        }
    }
    float pal[8];
    pal[0] = float(e0);
    pal[1] = float(e1);
    [unroll]
    for (uint k = 0; k < 6; ++k) {
        pal[k + 2u] = float((6u - k) * e0 + (k + 1u) * e1) / 7.0;
    }
    uint4 packed = uint4(0, 0, 0, 0);
    uint bitPos = 0;
    writeBits(packed, bitPos, e0, 8u);
    writeBits(packed, bitPos, e1, 8u);
    [unroll]
    for (uint i = 0; i < 16; ++i) {
        uint best = 0;
        float bestD = 1e30;
        [unroll]
        for (uint p = 0; p < 8; ++p) {
            const float d = abs(texel[i] - pal[p]);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
        writeBits(packed, bitPos, best, 3u);
    }
    gDst.Store(outOff, packed.x);
    gDst.Store(outOff + 4u, packed.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc4(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }
    storeBc4((tid.y * blocksX + tid.x) * 8u, 0u, tid);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc5(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }
    const uint base = (tid.y * blocksX + tid.x) * 16u;
    storeBc4(base, 0u, tid);
    storeBc4(base + 8u, 1u, tid);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void csBc3(uint3 tid : SV_DispatchThreadID) {
    const uint width = gSrc.Load(0);
    const uint height = gSrc.Load(4);
    const uint blocksX = (width + 3u) / 4u;
    const uint blocksY = (height + 3u) / 4u;
    if (tid.x >= blocksX || tid.y >= blocksY) {
        return;
    }
    float3 texel[16];
    float3 lo = float3(255.0, 255.0, 255.0);
    float3 hi = float3(0.0, 0.0, 0.0);
    [unroll]
    for (uint ty = 0; ty < 4; ++ty) {
        [unroll]
        for (uint tx = 0; tx < 4; ++tx) {
            const float3 c = loadTexel(width, height, tid.x * 4u + tx, tid.y * 4u + ty);
            texel[ty * 4u + tx] = c;
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }
    uint c0 = pack565(hi);
    uint c1 = pack565(lo);
    if (c0 <= c1) {
        if (c1 < 0xffffu) {
            c0 = c1 + 1u;
        } else {
            c1 = c0 - 1u;
        }
    }
    const float3 color0 = unpack565(c0);
    const float3 color1 = unpack565(c1);
    const float3 color2 = (color0 * 2.0 + color1) / 3.0;
    const float3 color3 = (color0 + color1 * 2.0) / 3.0;
    uint indices = 0;
    [unroll]
    for (uint i = 0; i < 16; ++i) {
        const float3 c = texel[i];
        const float d0 = dot(c - color0, c - color0);
        const float d1 = dot(c - color1, c - color1);
        const float d2 = dot(c - color2, c - color2);
        const float d3 = dot(c - color3, c - color3);
        uint best = 0;
        float bestD = d0;
        if (d1 < bestD) { best = 1; bestD = d1; }
        if (d2 < bestD) { best = 2; bestD = d2; }
        if (d3 < bestD) { best = 3; bestD = d3; }
        indices |= best << (i * 2);
    }
    const uint base = (tid.y * blocksX + tid.x) * 16u;
    gDst.Store(base, c0 | (c1 << 16));
    gDst.Store(base + 4u, indices);
    storeBc4(base + 8u, 3u, tid);
}
