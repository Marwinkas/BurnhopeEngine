#include "rc_common.slang"

[shader("compute")]
[numthreads(64, 1, 1)]
void rcMerge(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex, uint3 gid : SV_GroupID) {
    const uint cascade = gPush.cascadeIndex;
    const uint live = min(min(rcCount(cascade), rcProbeCap(cascade)), 65535u);
    const uint probe = gid.x;
    const uint local = gid.y * 64u + gtid;
    if (cascade == 3u) {
        if (probe >= live || local >= rcDirCount(3u)) {
            return;
        }
        float3 J;
        float beta;
        float count;
        rcLoadAccum(3u, probe, local, J, beta, count);
        const float valid = count >= 1.0 ? 1.0 : 0.0;
        rcStoreMerged(3u, probe, local, count >= 1.0 ? J : float3(0.0, 0.0, 0.0), valid);
        return;
    }
    const uint sub = local & 3u;
    const uint coarse = local >> 2u;
    const bool use = probe < live && local < rcDirCount(cascade + 1u);
    float3 childI = 0.0;
    float childV = 0.0;
    if (use) {
        const uint parent = rcParent(cascade, probe);
        if (parent != kInvalid && parent < rcProbeCap(cascade + 1u)) {
            rcLoadMerged(cascade + 1u, parent, local, childI, childV);
            if (childV < 0.5) {
                childI = 0.0;
                childV = 0.0;
            } else {
                childV = 1.0;
            }
        }
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
    if (!use || sub != 0u) {
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
    const float3 parentAvg = validSum > 0.5 ? (sum / validSum) : float3(0.0, 0.0, 0.0);
    const float3 I = J + beta * parentAvg;
    rcStoreMerged(cascade, probe, coarse, I, 1.0);
}