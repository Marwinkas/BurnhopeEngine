#include "core/math.slang"
#include "rc_common.slang"

// 6×6 октаэдр: внутренние 4×4 — свёртка Ламберта 32 направлений c0.
// Рамка в один тексель — продолжение через ребро октаэдра, чтобы билинейный фильтр не рвал шов.
int2 rcBorderSrc(int x, int y) {
    const int R = 6;
    if (x == 0 && y > 0 && y < R - 1) {
        return int2(R - 2, R - 1 - y);
    }
    if (x == R - 1 && y > 0 && y < R - 1) {
        return int2(1, R - 1 - y);
    }
    if (y == 0 && x > 0 && x < R - 1) {
        return int2(R - 1 - x, R - 2);
    }
    if (y == R - 1 && x > 0 && x < R - 1) {
        return int2(R - 1 - x, 1);
    }
    if (x == 0 && y == 0) {
        return int2(R - 2, R - 2);
    }
    if (x == R - 1 && y == 0) {
        return int2(1, R - 2);
    }
    if (x == 0 && y == R - 1) {
        return int2(R - 2, 1);
    }
    return int2(1, 1);
}

// Косинус по всем 32 бинам. Пустой бин — ноль в числителе и остаётся в знаменателе,
// иначе один жёлтый удар закрашивает всю полусферу.
float3 rcConvolve(uint probe, float3 axis) {
    float3 acc = 0.0;
    float w = 0.0;
    [loop]
    for (uint d = 0u; d < 32u; ++d) {
        const float rawNd = max(dot(rcDecodeGrid(0u, d), axis), 0.0);
        const float nd = rawNd * sqrt(rawNd);
        w += nd;
        float3 I;
        float valid;
        rcLoadMerged(0u, probe, d, I, valid);
        if (valid < 0.5) {
            continue;
        }
        acc += I * nd;
    }
    return w > 1e-4 ? acc / w : 0.0;
}
[shader("compute")]
[numthreads(64, 1, 1)]
void rcOctahedral(uint3 dtid : SV_DispatchThreadID) {
    const uint probe = dtid.x;
    if (probe == 0u) {
        rcStore(220u, rcLoad(rcMetaAddr(0u, 0u) + 24u));
        rcStore(228u, rcCount(0u));
        rcStore(240u, rcCount(1u));
        rcStore(244u, rcCount(2u));
        rcStore(248u, rcCount(3u));
    }
    if (probe >= min(rcCount(0u), rcProbeCap(0u)) || rcLoad(rcMetaAddr(0u, probe) + 24u) == 0u) {
        return;
    }
    float3 tex[36];
    [loop]
    for (uint i = 0u; i < 36u; ++i) {
        tex[i] = 0.0;
    }
    [loop]
    for (uint y = 1u; y <= 4u; ++y) {
        [loop]
        for (uint x = 1u; x <= 4u; ++x) {
            const float2 uv = (float2(float(x - 1u), float(y - 1u)) + 0.5) / 4.0;
            tex[y * 6u + x] = rcConvolve(probe, octDecode(uv));
        }
    }
    [loop]
    for (uint y = 0u; y < 6u; ++y) {
        [loop]
        for (uint x = 0u; x < 6u; ++x) {
            if (x >= 1u && x <= 4u && y >= 1u && y <= 4u) {
                continue;
            }
            const int2 src = rcBorderSrc(int(x), int(y));
            tex[y * 6u + x] = tex[uint(src.y) * 6u + uint(src.x)];
        }
    }
    if (probe == 0u) {
        rcStore(224u, asuint(tex[7].x));
    }
    const uint base = sliceBase() + rcIrrRel() + probe * rcIrrStride();
    [loop]
    for (uint i = 0u; i < 36u; ++i) {
        const uint off = base + i * 16u;
        rcStore(off, asuint(tex[i].x));
        rcStore(off + 4u, asuint(tex[i].y));
        rcStore(off + 8u, asuint(tex[i].z));
        rcStore(off + 12u, asuint(1.0));
    }
}
