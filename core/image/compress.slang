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
