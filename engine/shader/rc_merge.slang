#include "rc_common.slang"

// Слияние от c3 к c0. Четыре направления верхнего каскада лежат подряд.
// Warp читает их через WaveReadLaneAt и пишет уравнение
// I = J + β * (среднее детей, у которых есть замер).
// Бин без лучей остаётся невалидным: это не промах и не чёрный ноль.
// Промах (count > 0, β → 1) пропускает родителя. Попадание (β → 0) его закрывает.
[shader("compute")]
[numthreads(64, 1, 1)]
void rcMerge(uint3 dtid : SV_DispatchThreadID) {
    const uint cascade = gPush.cascadeIndex;
    const uint live = min(rcCount(cascade), rcProbeCap(cascade));
    if (cascade == 3u) {
        const uint dirs = rcDirCount(3u);
        const uint probe = dtid.x / dirs;
        const uint dir = dtid.x - probe * dirs;
        if (probe >= live) {
            return;
        }
        float3 J;
        float beta;
        float count;
        rcLoadAccum(3u, probe, dir, J, beta, count);
        const float valid = count >= 1.0 ? 1.0 : 0.0;
        rcStoreMerged(3u, probe, dir, count >= 1.0 ? J : float3(0.0, 0.0, 0.0), valid);
        return;
    }
    const uint fine = rcDirCount(cascade + 1u);
    const uint probe = dtid.x / fine;
    const uint local = dtid.x - probe * fine;
    if (probe >= live) {
        return;
    }
    const uint sub = local & 3u;
    const uint coarse = local >> 2u;
    const uint parent = rcParent(cascade, probe);
    float3 childI = 0.0;
    float childV = 0.0;
    if (parent != kInvalid && parent < rcProbeCap(cascade + 1u)) {
        rcLoadMerged(cascade + 1u, parent, local, childI, childV);
    }
    const uint lane = WaveGetLaneIndex();
    const uint baseLane = lane & ~3u;
    float3 sum = 0.0;
    float validSum = 0.0;
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        const uint src = baseLane + i;
        sum += float3(
            WaveReadLaneAt(childI.x, src),
            WaveReadLaneAt(childI.y, src),
            WaveReadLaneAt(childI.z, src));
        validSum += WaveReadLaneAt(childV, src);
    }
    if (sub != 0u) {
        return;
    }
    float3 J;
    float beta;
    float count;
    rcLoadAccum(cascade, probe, coarse, J, beta, count);
    if (count < 1.0) {
        rcStoreMerged(cascade, probe, coarse, float3(0.0, 0.0, 0.0), 0.0);
        return;
    }
    // Пустой сосед не измерен. Деление на четыре гасит одно попадание в 1/64 к c0.
    const float3 parentI = validSum > 0.0 ? sum / validSum : float3(0.0, 0.0, 0.0);
    const float3 I = J + beta * parentI;
    rcStoreMerged(cascade, probe, coarse, I, 1.0);
}
