// Туман поверх уже записанного HdrA. Солнце и лампы не трогает.

#include "frame.slang"
#include "camera.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(15, 0)]] Texture2D<float> gShadow0;

#include "shade_shadow.slang"

[shader("compute")]
[numthreads(16, 16, 1)]
void shadeFog(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    if ((gPass.passMask & 4096u) == 0u || gShade.fogDensity <= 0.0008 || gFrame.debugMode != 0) {
        return;
    }
    const uint2 px = dtid.xy;
    const uint vis = gVis.Load(int3(px, 0));
    const float depth = gDepth.Load(int3(px, 0));
    const float3 cam = gFrame.cameraPos.xyz;
    float rayLen = 64.0;
    float3 rayDir = float3(0.0, 0.0, 1.0);
    if (vis != 0u && depth > 0.001) {
        const float3 P = reconstructWorld(px, depth);
        const float3 ray = P - cam;
        rayLen = max(length(ray), 0.05);
        rayDir = ray / rayLen;
    } else {
        const float3 farP = reconstructWorld(px, 0.0);
        const float3 ray = farP - cam;
        const float farLen = max(length(ray), 0.05);
        rayDir = ray / farLen;
        rayLen = 64.0;
    }
    const float march = min(rayLen, 64.0);
    const int fogSteps = 16;
    const float stepLen = march / float(fogSteps);
    float transmit = 1.0;
    float3 fogLight = 0.0;
    [loop]
    for (int i = 0; i < fogSteps; ++i) {
        const float t = (float(i) + 0.5) * stepLen;
        const float3 Q = cam + rayDir * t;
        const float height = exp(-max(Q.y - gShade.fogHeight, 0.0) * 0.12);
        const float optical = gShade.fogDensity * height * stepLen;
        const float absorb = 1.0 - exp(-optical);
        const float sunVis = gFrame.disableShadow == 0 ? cascadeFogLit(Q) : 1.0;
        const float3 sun = gSun.sunColor.xyz * gSun.sunDirIntensity.w * sunVis * gShade.fogScatter;
        fogLight += transmit * absorb * (sun + float3(0.04, 0.045, 0.05));
        transmit *= 1.0 - absorb;
    }
    const float4 hdr = gHdr[px];
    gHdr[px] = float4(hdr.rgb * transmit + fogLight, hdr.a);
}
