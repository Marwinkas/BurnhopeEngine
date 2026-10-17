#include "vx_common.slang"

[[vk::binding(17, 0)]]
RWStructuredBuffer<uint> gAtom;

[[vk::binding(60, 0)]]
RWTexture3D<float4> gStore[6];

[shader("compute")]
[numthreads(64, 1, 1)]
void vxResolve(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= kVxN) {
        return;
    }
    const int x = int(dtid.x % kVxRes);
    const int y = int((dtid.x / kVxRes) % kVxRes);
    const int z = int(dtid.x / (kVxRes * kVxRes));
    const VxCascade cas = vxLevel(gPush.cascade);
    const int3 tex = (int3(x, y, z) + vxScroll(cas)) & int(kVxRes - 1u);
    const uint at = vxAtomBase() + uint(tex.x + tex.y * int(kVxRes) + tex.z * int(kVxRes * kVxRes)) * 4u;
    const float r = float(gAtom[at]) * (1.0 / 255.0);
    const float g = float(gAtom[at + 1u]) * (1.0 / 255.0);
    const float b = float(gAtom[at + 2u]) * (1.0 / 255.0);
    const float a = gAtom[at + 3u] > 0u ? 1.0 : 0.0;
    gStore[vxImage()][uint3(tex)] = float4(r, g, b, a);
}
