// Небо из карты полусферы. Поверхность затирает пиксель следующим проходом.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

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
    float3 sky = gSkyView.SampleLevel(gClamp, atmSkyUv(rd), 0.0).rgb;
    const bool sunOn = (gPass.passMask & 1024u) != 0u && (gPass.passMask & 256u) != 0u;
    if (!sunOn) {
        sky *= 0.03;
    } else {
        const float3 sun = atmSunDir(gAtm);
        const float sunDot = dot(rd, sun);
        sky *= 0.25 + 0.75 * saturate(sunDot * 0.65 + 0.35);
        const float glow = cos(1.05);
        const float disk = cos(0.22);
        if (sunDot > glow && rd.y > -0.2) {
            const float core = saturate((sunDot - disk) / max(1.0 - disk, 1e-4));
            const float wide = saturate((sunDot - glow) / max(1.0 - glow, 1e-4));
            sky += atmSunIllum(gAtm, sun) * (wide * 0.55 + core * core * 36.0);
        }
    }
    gHdr[dtid.xy] = float4(max(sky, float3(0.0, 0.0, 0.0)), 1.0);
}
