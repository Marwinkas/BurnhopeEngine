#include "frame.slang"
#include "core/hiz_atlas.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(6, 0)]] RWTexture2D<float> gHiZ;

float hizLoad(int2 p) {
    return gHiZ.Load(p);
}

void hizStore(uint2 p, float v) {
    gHiZ[p] = v;
}

#ifndef HIZ_MIP
#define HIZ_MIP 0
#endif
#ifndef HIZ_MAX
#define HIZ_MAX 0
#endif

uint mipY(uint mip) {
    return atlasMipY(mip, gFrame.extent.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void hizCs(uint3 dtid : SV_DispatchThreadID) {
    const uint mip = HIZ_MIP;
    const uint2 ext = max(gFrame.extent.xy >> mip, uint2(1, 1));
    if (dtid.x >= ext.x || dtid.y >= ext.y) {
        return;
    }
    float d = 1.0;
    if (mip == 0) {
        d = gDepth.Load(int3(dtid.xy, 0));
    } else {
        const uint2 base = dtid.xy * 2;
        const uint2 prev = max(gFrame.extent.xy >> (mip - 1), uint2(1, 1));
        const uint y = mipY(mip - 1);
        d = hizLoad(int2(int(min(base.x, prev.x - 1)), int(y + min(base.y, prev.y - 1))));
        const float b = hizLoad(int2(int(min(base.x + 1, prev.x - 1)), int(y + min(base.y, prev.y - 1))));
        const float c = hizLoad(int2(int(min(base.x, prev.x - 1)), int(y + min(base.y + 1, prev.y - 1))));
        const float e = hizLoad(int2(int(min(base.x + 1, prev.x - 1)), int(y + min(base.y + 1, prev.y - 1))));
#if HIZ_MAX
        d = max(max(d, b), max(c, e));
#else
        d = min(min(d, b), min(c, e));
#endif
    }
    hizStore(uint2(dtid.x, mipY(mip) + dtid.y), d);
}
