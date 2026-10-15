#include "rc_common.slang"

// Иерархическая префиксная сумма числа лучей зонда c0.
// Проход 0 — скан блока 512. Проход 1 — скан сумм блоков и аргументы indirect.
// Проход 2 — прибавка базы блока. Каждому зонду достаётся свой отрезок R2.
groupshared uint gScan[512];

[shader("compute")]
[numthreads(512, 1, 1)]
void rcPrefix(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex, uint3 gid : SV_GroupID) {
    const uint pass = gPush.cascadeIndex;
    const uint cap = rcProbeCap(0u);
    if (pass == 0u) {
        const uint i = gid.x * 512u + gtid;
        const uint self = i < cap ? rcLoad(rcMetaAddr(0u, i) + 20u) : 0u;
        gScan[gtid] = self;
        GroupMemoryBarrierWithGroupSync();
        [unroll]
        for (uint offset = 1u; offset < 512u; offset <<= 1u) {
            uint v = gScan[gtid];
            if (gtid >= offset) {
                v += gScan[gtid - offset];
            }
            GroupMemoryBarrierWithGroupSync();
            gScan[gtid] = v;
            GroupMemoryBarrierWithGroupSync();
        }
        if (i < cap) {
            rcStore(rcPrefixAddr(i), gScan[gtid] - self);
        }
        if (gtid == 511u && gid.x < rcScanBlocks()) {
            rcStore(rcBlockSumAddr(gid.x), gScan[511u]);
        }
        return;
    }
    if (pass == 1u) {
        if (gtid != 0u) {
            return;
        }
        uint sum = 0u;
        const uint blocks = rcScanBlocks();
        [loop]
        for (uint b = 0u; b < blocks; ++b) {
            rcStore(rcBlockExcAddr(b), sum);
            sum += rcLoad(rcBlockSumAddr(b));
        }
        [unroll]
        for (uint c = 0u; c < 4u; ++c) {
            const uint n = min(rcCount(c), rcProbeCap(c));
            const uint dirs = c == 3u ? rcDirCount(3u) : rcDirCount(c + 1u);
            const uint groups = (n * dirs + 63u) / 64u;
            const uint addr = rcArgsAddr(c);
            rcStore(addr, groups);
            rcStore(addr + 4u, 1u);
            rcStore(addr + 8u, 1u);
            rcStore(addr + 12u, 0u);
        }
        const uint live = min(rcCount(0u), rcProbeCap(0u));
        const uint traceAddr = rcArgsAddr(5u);
        rcStore(traceAddr, (live * 32u + 63u) / 64u);
        rcStore(traceAddr + 4u, 1u);
        rcStore(traceAddr + 8u, 1u);
        rcStore(traceAddr + 12u, 0u);
        const uint octAddr = rcArgsAddr(4u);
        rcStore(octAddr, (live + 63u) / 64u);
        rcStore(octAddr + 4u, 1u);
        rcStore(octAddr + 8u, 1u);
        rcStore(octAddr + 12u, 0u);
        return;
    }
    const uint i = gid.x * 512u + gtid;
    if (i < cap && gid.x < rcScanBlocks()) {
        rcStore(rcPrefixAddr(i), rcLoad(rcPrefixAddr(i)) + rcLoad(rcBlockExcAddr(gid.x)));
    }
}
