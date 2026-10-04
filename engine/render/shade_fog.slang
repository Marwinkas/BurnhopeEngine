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
    if (gVis.Load(int3(px, 0)) == 0u) {
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(px, depth);
    const float3 cam = gFrame.cameraPos.xyz;
    const float3 ray = P - cam;
    const float rayLen = max(length(ray), 0.05);
    const float3 rayDir = ray / rayLen;
    const int fogSteps = 16;
    const float zn = max(gFrame.cameraPos.w, 0.05);
    const float zf = 80.0;
    float transmit = 1.0;
    float3 fogLight = 0.0;
    [loop]
    for (int i = 1; i <= fogSteps; ++i) {
        const float z0 = zn * pow(zf / zn, float(i - 1) / float(fogSteps));
        const float z1 = zn * pow(zf / zn, float(i) / float(fogSteps));
        if (z0 >= rayLen) {
            break;
        }
        const float stepLen = min(z1, rayLen) - z0;
        const float3 Q = cam + rayDir * min(z1, rayLen);
        const float height = exp(-max(Q.y - gShade.fogHeight, 0.0) * 0.12);
        const float optical = gShade.fogDensity * height * stepLen;
        const float absorb = 1.0 - exp(-optical);
        const float sunVis = gFrame.disableShadow == 0 ? cascadeLit(2u, Q) : 1.0;
        const float3 sun = gSun.sunColor.xyz * gSun.sunDirIntensity.w * sunVis * gShade.fogScatter;
        fogLight += transmit * absorb * (sun + float3(0.04, 0.045, 0.05));
        transmit *= 1.0 - absorb;
    }
    const float4 hdr = gHdr[px];
    gHdr[px] = float4(hdr.rgb * transmit + fogLight, hdr.a);
}
