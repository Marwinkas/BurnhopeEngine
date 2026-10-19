// Filament global fog (Wenzel). Цвет и солнечные лучи живут в кадре, не в карте теней.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(13, 0)]] SamplerState gSamp;
[[vk::binding(42, 0)]] Texture2D<float4> gSky;

[shader("compute")]
[numthreads(16, 16, 1)]
void shadeFog(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    if ((gPass.passMask & 4096u) == 0u || gFrame.debugMode != 0u) {
        return;
    }
    const float density = max(gShade.fogDensity, 0.0);
    const float inSize = gShade.fogScatter;
    if (density <= 1e-5 && inSize <= 0.0) {
        return;
    }
    const uint2 px = dtid.xy;
    const uint vis = gVis.Load(int3(px, 0));
    const float depth = gDepth.Load(int3(px, 0));
    const float3 cam = gFrame.cameraPos.xyz;
    float3 ray = float3(0.0, 0.0, 1.0);
    if (vis != 0u && depth > 0.001) {
        ray = reconstructWorld(px, depth) - cam;
    } else {
        ray = reconstructWorld(px, 0.0) - cam;
    }
    float d = length(ray);
    if (d < 1e-3) {
        return;
    }
    const float3 view = ray / d;
    d = min(d, 800.0);
    const float fogStart = max(gRcParams.fogStart, 0.0);
    if (d < fogStart) {
        return;
    }

    const float falloff = max(gRcParams.fogFalloff, 0.0);
    const float eye = cam.y - gShade.fogHeight;
    const float densityY = -falloff * eye;
    const float densityZ = density * exp(densityY);
    const float3 segment = view * d;
    const float fh = falloff * segment.y;
    float opticalPerMeter = densityZ;
    if (falloff < 1e-4) {
        opticalPerMeter = density;
    } else if (abs(fh) > 0.00125) {
        opticalPerMeter = (densityZ - density * exp(densityY - fh)) / fh;
    }
    opticalPerMeter = max(opticalPerMeter, 0.0);
    const float optical = opticalPerMeter * max(d - fogStart, 0.0);
    const float transmit = exp(-optical);
    const float opacity = min(saturate(1.0 - transmit), saturate(gRcParams.fogOpacity));

    const float3 skyLit = max(gSky.SampleLevel(gSamp, atmSkyUv(view), 0.0).rgb, 0.0);
    const float skyLum = dot(skyLit, float3(0.2126, 0.7152, 0.0722));
    const float3 chroma = skyLum > 0.05 ? skyLit / skyLum : float3(1.0, 1.0, 1.0);
    const float3 tint = max(float3(gRcParams.fogR, gRcParams.fogG, gRcParams.fogB), 0.0);
    float3 fogColor = tint * chroma * max(gRcParams.fogLum, 0.0) * opacity;

    if (inSize > 0.0 && (gPass.passMask & 256u) != 0u) {
        const float3 sunRaw = gSun.sunDirIntensity.xyz;
        const float3 sunDir = dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
        const float inStart = max(gRcParams.fogInStart, 0.0);
        const float sunOptical = opticalPerMeter * max(d - inStart, 0.0);
        const float sunTrans = exp(-sunOptical);
        const float sunAmount = saturate(dot(view, sunDir));
        const float sunIn = pow(sunAmount, inSize);
        float3 sunCol = gSun.sunColor.xyz;
        if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
            sunCol = float3(1.0, 0.92, 0.76);
        }
        float inten = gSun.sunDirIntensity.w;
        if (inten < 0.05) {
            inten = 8.0;
        }
        fogColor += sunCol * inten * sunIn * (1.0 - sunTrans);
    }

    const float4 hdr = gHdr[px];
    gHdr[px] = float4(hdr.rgb * transmit + fogColor, hdr.a);
}
