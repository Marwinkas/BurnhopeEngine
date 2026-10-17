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
    const float falloff = 0.12;
    const float density = gShade.fogDensity;
    const float3 view = rayDir * rayLen;
    const float atCam = density * exp(-falloff * (cam.y - gShade.fogHeight));
    const float fh = falloff * view.y;
    float opticalPerMeter = atCam;
    if (abs(fh) > 0.00125) {
        const float atEnd = density * exp(-falloff * (cam.y + view.y - gShade.fogHeight));
        opticalPerMeter = (atCam - atEnd) / fh;
    }
    const float optical = max(opticalPerMeter, 0.0) * rayLen;
    const float transmit = exp(-optical);
    const float opacity = saturate(1.0 - transmit);
    const float sunVis = gFrame.disableShadow == 0 ? cascadeFogLit(cam + view) : 1.0;
    const float3 sunDir = gSun.sunDirIntensity.xyz;
    const float sunAmt = pow(saturate(dot(rayDir, sunDir)), 8.0);
    const float3 sunCol = gSun.sunColor.xyz * gSun.sunDirIntensity.w * sunVis * gShade.fogScatter;
    const float3 fogCol = (float3(0.62, 0.68, 0.74) * 0.08 + sunCol * sunAmt) * opacity;
    const float4 hdr = gHdr[px];
    gHdr[px] = float4(hdr.rgb * transmit + fogCol, hdr.a);
}
