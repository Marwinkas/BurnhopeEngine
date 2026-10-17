#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "rc_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;

#include "rc_surface.slang"

uint rcPixelScore(int3 cell, uint tag, uint2 px, float3 P) {
    const float3 center = rcCenter(cell, 0u, tag);
    const float dist = length(P - center);
    const uint distScore = 0x1FFFFu - min(uint(dist * 500.0 + 0.5), 0x1FFFEu);
    const uint tieBreak = ((px.x * 1664525u) ^ (px.y * 1013904223u)) & 0x3FFFu;
    return max((distScore << 14u) | tieBreak, 1u);
}

void rcTouchCell(uint stage, int3 cell, uint tag, uint2 px, float3 P, float3 N) {
    if (stage == 0u) {
        if (!rcEnsureProbe(0u, cell, tag, P, N)) {
            return;
        }
        const float3 center = rcCenter(cell, 0u, tag);
        [unroll]
        for (uint n = 1u; n < 4u; ++n) {
            const int3 up = rcCell(center, n, tag);
            rcEnsureProbe(n, up, tag, P, N);
        }
        return;
    }

    const uint probe = rcLookup(0u, rcPackKey(cell, tag));
    if (probe == kInvalid) {
        return;
    }
    const uint meta = rcMetaAddr(0u, probe);
    const uint score = rcPixelScore(cell, tag, px, P);

    if (stage == 1u) {
        uint prev = 0u;
        gRc.InterlockedMax(meta + 28u, score, prev);
        return;
    }

    if (rcLoad(meta + 28u) == score) {
        rcStore(meta + 32u, asuint(P.x));
        rcStore(meta + 36u, asuint(P.y));
        rcStore(meta + 40u, asuint(P.z));
        rcStore(meta + 44u, asuint(N.x));
        rcStore(meta + 48u, asuint(N.y));
        rcStore(meta + 52u, asuint(N.z));
    }
}

void rcInsertLodBox(uint stage, uint2 px, float3 P, float3 N, uint lod) {
    const uint tag = lod & 7u;
    const float sp = max(rcSpacing(0u, tag), 1e-3);
    const float3 sampleP = P + N * 0.02;
    const float3 g = sampleP / sp - 0.5;
    const int3 i0 = int3(floor(g));
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const int3 cell = i0 + int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        rcTouchCell(stage, cell, tag, px, P, N);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void rcAlloc(uint3 dtid : SV_DispatchThreadID) {
    const uint stage = gPush.cascadeIndex;
    const uint tileW = max((gFrame.extent.x + 1u) >> 1u, 1u);
    const uint tileH = max((gFrame.extent.y + 1u) >> 1u, 1u);
    const uint totalTiles = tileW * tileH;

    [loop]
    for (uint idx = dtid.x; idx < totalTiles; idx += 4096u * 64u) {
        const uint ty = idx / tileW;
        const uint tx = idx - ty * tileW;
        const uint2 px = uint2(tx * 2u, ty * 2u);
        float depth = 0.0;
        if (!rcPixel(px, depth)) {
            continue;
        }
        const float3 P = reconstructWorld(px, depth);
        float3 N = octDecode(gSurf1[px].xy);
        if (dot(N, N) < 0.5) {
            N = reconstructNormalFromDepth(gDepth, px, P);
        } else {
            N = normalize(N);
        }
        const float cheb = rcChebDist(P, gFrame.cameraPos.xyz);
        const float d0 = rcLodBaseRadius();
        const uint lod = cheb <= d0 ? 0u : min(uint(floor(log2(cheb / d0))), 5u);
        rcInsertLodBox(stage, px, P, N, lod);

        const float lodEnd = d0 * exp2(float(lod));
        if (cheb >= lodEnd * 0.85 && lod < 5u) {
            rcInsertLodBox(stage, px, P, N, lod + 1u);
        }
    }
}