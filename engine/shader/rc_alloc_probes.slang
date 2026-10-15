#include "frame.slang"
#include "camera.slang"
#include "rc_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;

#include "rc_surface.slang"

// c0 и три родителя. Возвращает индекс c0. Тот же ключ у всех пикселей клетки.
uint rcChain(int3 cell0, uint lod) {
    const uint p0 = rcInsert(0u, rcPackKey(cell0, lod), cell0, lod);
    if (p0 == kInvalid) {
        return kInvalid;
    }
    uint child = p0;
    int3 cell = cell0;
    [unroll]
    for (uint c = 0u; c < 3u; ++c) {
        const float3 center = rcCenter(cell, c, lod);
        const int3 parentCell = rcCell(center, c + 1u, lod);
        const uint parent = rcInsert(c + 1u, rcPackKey(parentCell, lod), parentCell, lod);
        if (parent == kInvalid) {
            return p0;
        }
        rcStore(rcMetaAddr(c, child) + 16u, parent);
        child = parent;
        cell = parentCell;
    }
    return p0;
}

// Меньший индекс пикселя побеждает. Оба кадра в полёте берут одну и ту же точку.
void rcAnchor(uint probe, uint2 px) {
    if (probe == kInvalid || gFrame.extent.x == 0u) {
        return;
    }
    const uint score = px.y * gFrame.extent.x + px.x;
    uint prev = 0u;
    gRc.InterlockedMin(rcMetaAddr(0u, probe) + 28u, score, prev);
}

// Восемь углов, которые шейдинг смешивает. Пустой угол даёт чёрную клетку,
// и набор таких углов меняется вместе с камерой.
void rcCover(float3 P, uint lod) {
    const float sp = max(rcSpacing(0u, lod), 1e-3);
    const int3 i0 = int3(floor(P / sp - 0.5));
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const int3 cell = i0 + int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        rcChain(cell, lod);
    }
}

// Пиксель закрывает свою клетку и соседний LOD. Шейдинг смешивает оба.
[shader("compute")]
[numthreads(8, 8, 1)]
void rcAlloc(uint3 dtid : SV_DispatchThreadID) {
    float depth = 0.0;
    if (!rcPixel(dtid.xy, depth)) {
        return;
    }
    const float3 P = reconstructWorld(dtid.xy, depth);
    const uint lod0 = rcLod(P);
    const uint lod1 = min(lod0 + 1u, 5u);
    const uint p0 = rcChain(rcCell(P, 0u, lod0), lod0);
    rcAnchor(p0, dtid.xy);
    rcCover(P, lod0);
    if (lod1 != lod0) {
        rcAnchor(rcChain(rcCell(P, 0u, lod1), lod1), dtid.xy);
        rcCover(P, lod1);
    }
}
