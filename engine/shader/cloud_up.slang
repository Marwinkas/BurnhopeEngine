#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(45, 0)]] Texture2D<float4> gCloudSample;
[[vk::binding(23, 0)]] SamplerState gClamp;

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudUpCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gVis.Load(int3(px, 0)) != 0u || gDepth.Load(int3(px, 0)) > 1e-6) {
        return;
    }
    const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
    const float2 texel = gFrame.invExtent * 2.0;

    // 3x3 Tent filter over half-res cloud buffer for silky-smooth, noise-free cloud edges
    const float w[3] = {0.25, 0.5, 0.25};
    float4 cloud = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = w[x + 1] * w[y + 1];
            cloud += gCloudSample.SampleLevel(gClamp, uv + float2(float(x), float(y)) * texel * 0.75, 0.0) * weight;
        }
    }

    const float4 bg = gHdr[px];
    const float3 composed = bg.rgb * cloud.a + cloud.rgb;
    gHdr[px] = float4(composed, bg.a);
}