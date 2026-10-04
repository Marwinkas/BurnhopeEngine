// Небо из карты полусферы. Поверхность затирает пиксель следующим проходом.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;
[[vk::binding(23, 0)]] SamplerState gClamp;

[shader("compute")]
[numthreads(16, 16, 1)]
void shadeSky(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    if ((gPass.passMask & 1024u) == 0u) {
        gHdr[dtid.xy] = float4(0.03, 0.035, 0.045, 1.0);
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) * gFrame.invExtent;
    const float3 farP = reconstructWorld(uv, 0.0);
    const float3 rd = normalize(farP - gFrame.cameraPos.xyz);
    const float3 sky = gSkyView.SampleLevel(gClamp, atmSkyUv(rd), 0.0).rgb;
    gHdr[dtid.xy] = float4(max(sky, float3(0.0, 0.0, 0.0)), 1.0);
}
