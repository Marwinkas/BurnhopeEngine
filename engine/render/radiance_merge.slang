#include "frame.slang"
#include "core/math.slang"
#include "radiance_rc.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void rcMerge(uint3 dtid : SV_DispatchThreadID) {
    const uint cascade = rcLoad(144u);
    const uint dirs = dirCountOf(cascade);
    const uint probe = dtid.x / dirs;
    const uint dir = dtid.x - probe * dirs;
    if (probe >= rcLoad(countByte(cascade)) || probe >= probeCap(cascade)) {
        return;
    }
    float count = 0.0;
    float beta = 1.0;
    const float3 J = loadInterval(cascade, probe, dir, count, beta);
    if (count < 1.0) {
        return;
    }
    float3 parent = 0.0;
    if (cascade < 3u) {
        const uint meta = metaBase(cascade) + probe * 32u;
        const uint lod = rcLoad(meta + 12u);
        if (rcLoad(148u) != 0xFFFFFFFFu && lod != rcLoad(148u)) {
            return;
        }
        const int3 cell = int3(asint(rcLoad(meta)), asint(rcLoad(meta + 4u)), asint(rcLoad(meta + 8u)));
        const float sp = spacingAt(cascade, lod);
        const float3 center = (float3(cell) + 0.5) * sp;
        const int3 pcell = nearestCell(center, cascade + 1u, lod);
        const uint pprobe = lookupProbe(cascade + 1u, packKey(pcell, lod));
        const uint nu = (4u << cascade) * 2u;
        const uint u = dir % nu;
        const uint v = dir / nu;
        const uint nuP = nu * 2u;
        float3 acc = 0.0;
        float w = 0.0;
        [unroll]
        for (uint dv = 0u; dv < 2u; ++dv) {
            [unroll]
            for (uint du = 0u; du < 2u; ++du) {
                float childCount = 0.0;
                const float3 I = loadMerged(cascade + 1u, pprobe, (v * 2u + dv) * nuP + u * 2u + du, childCount);
                if (childCount >= 1.0) {
                    acc += I;
                    w += 1.0;
                }
            }
        }
        parent = w > 0.0 ? acc / w : 0.0;
    }
    storeMerged(cascade, probe, dir, J + beta * parent);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void rcIrradiance(uint3 dtid : SV_DispatchThreadID) {
    const uint probe = dtid.x;
    if (probe >= rcLoad(countByte(0u))) {
        return;
    }
    const uint base = irrBase() + probe * 576u;
    [loop]
    for (uint bin = 0u; bin < 36u; ++bin) {
        const uint x = bin % 6u;
        const uint y = bin / 6u;
        const float3 axis = octDecode((float2(x, y) + 0.5) / 6.0);
        float3 acc = 0.0;
        float w = 0.0;
        [loop]
        for (uint d = 0u; d < 32u; ++d) {
            float count = 0.0;
            const float3 I = loadMerged(0u, probe, d, count);
            if (count < 1.0) {
                continue;
            }
            const float nd = max(dot(decodeGrid(0u, d), axis), 0.0);
            acc += I * nd;
            w += nd;
        }
        const float3 mean = w > 1e-4 ? acc / w : 0.0;
        const uint off = base + bin * 16u;
        rcStore(off, asuint(mean.x));
        rcStore(off + 4u, asuint(mean.y));
        rcStore(off + 8u, asuint(mean.z));
    }
}
