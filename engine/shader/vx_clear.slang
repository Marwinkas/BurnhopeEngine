#include "vx_common.slang"

[[vk::binding(17, 0)]]
RWStructuredBuffer<uint> gAtom;

[shader("compute")]
[numthreads(64, 1, 1)]
void vxClear(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= kVxN) {
        return;
    }
    const VxCascade cas = vxLevel(gPush.cascade);
    const int x = int(dtid.x % kVxRes);
    const int y = int((dtid.x / kVxRes) % kVxRes);
    const int z = int(dtid.x / (kVxRes * kVxRes));
    const int wx = cas.anchorX + x;
    const int wy = cas.anchorY + y;
    const int wz = cas.anchorZ + z;
    if (cas.full == 0u && !vxDirty(cas, wx, wy, wz)) {
        return;
    }
    const int3 tex = (int3(x, y, z) + vxScroll(cas)) & int(kVxRes - 1u);
    const uint at = vxAtomBase() + uint(tex.x + tex.y * int(kVxRes) + tex.z * int(kVxRes * kVxRes)) * 4u;
    gAtom[at] = 0u;
    gAtom[at + 1u] = 0u;
    gAtom[at + 2u] = 0u;
    gAtom[at + 3u] = 0u;
}
