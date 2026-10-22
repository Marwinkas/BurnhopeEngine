// Объёмный реймарчинг кучевых облаков. Плотность — cloud_density.slang.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"
#include "cloud_density.slang"

[[vk::binding(44, 0)]] RWTexture2D<float4> gCloud;
[[vk::binding(46, 0)]] StructuredBuffer<float> gCloudNoise;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

float cloudHg(float cosTheta, float g) {
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

bool intersectCloudShell(float3 ro, float3 rd, float bottom, float top, out float t0, out float t1) {
    const float earthR = 320000.0;
    const float3 center = float3(ro.x, -earthR, ro.z);
    const float3 oc = ro - center;
    const float b = dot(oc, rd);
    const float c = dot(oc, oc);
    const float rBot = earthR + bottom;
    const float rTop = earthR + top;
    const float discBot = b * b - (c - rBot * rBot);
    const float discTop = b * b - (c - rTop * rTop);
    if (discTop < 0.0) {
        t0 = 0.0;
        t1 = 0.0;
        return false;
    }
    const float tBot = discBot >= 0.0 ? (-b + sqrt(discBot)) : 0.0;
    const float tTop = -b + sqrt(discTop);
    t0 = max(tBot, 0.0);
    t1 = min(tTop, 32000.0);
    return t1 > t0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudMarchCs(uint3 dtid : SV_DispatchThreadID) {
    const uint2 halfExt = max(gFrame.extent.xy >> 1, uint2(1, 1));
    if (dtid.x >= halfExt.x || dtid.y >= halfExt.y) {
        return;
    }
    if (gAtm.cloudCoverage <= 0.01 || gAtm.cloudDensity <= 0.0005) {
        gCloud[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(halfExt);
    const float3 farWorld = reconstructWorld(uv, 1e-5);
    const float3 rd = normalize(farWorld - gFrame.cameraPos.xyz);
    if (rd.y <= -0.02) {
        gCloud[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const float bottom = max(gAtm.cloudBottom, 1200.0);
    const float top = max(gAtm.cloudTop, bottom + 2200.0);
    float t0 = 0.0;
    float t1 = 0.0;
    if (!intersectCloudShell(gFrame.cameraPos.xyz, rd, bottom, top, t0, t1)) {
        gCloud[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const float3 sunDir = atmSunDir(gAtm);
    const float cosTheta = dot(rd, sunDir);
    const float sunElev = saturate(sunDir.y * 1.8 + 0.08);
    const float3 sunColor = lerp(float3(1.0, 0.45, 0.18), float3(1.0, 0.98, 0.94), saturate(sunDir.y * 2.2)) * (2.6 * sunElev + 0.15);
    const float3 ambientTop = lerp(float3(0.28, 0.24, 0.36), float3(0.52, 0.68, 0.92), sunElev);
    const float3 ambientBot = lerp(float3(0.14, 0.12, 0.20), float3(0.26, 0.34, 0.48), sunElev);
    const int steps = 32;
    const float marchSpan = min(t1 - t0, 16000.0);
    const float stepSize = marchSpan / float(steps);
    const float distDitherFade = saturate(1.0 - t0 / 18000.0);
    const float dither = (frac(52.9829189 * frac(0.06711056 * float(dtid.x) + 0.00583715 * float(dtid.y))) - 0.5) * 0.35 * distDitherFade;
    const float extCoeff = gAtm.cloudDensity * 0.024;
    const float earthR = 320000.0;
    const float3 planetCenter = float3(gFrame.cameraPos.x, -earthR, gFrame.cameraPos.z);
    const float2 windOff = float2(gAtm.cloudSpeedX, gAtm.cloudSpeedZ) * gAtm.time;
    float transmittance = 1.0;
    float3 scattered = 0.0;
    [loop]
    for (int i = 0; i < steps; ++i) {
        if (transmittance < 0.01) {
            break;
        }
        const float t = t0 + (float(i) + 0.5 + dither) * stepSize;
        const float3 pos = gFrame.cameraPos.xyz + rd * t;
        const float sphericalY = length(pos - planetCenter) - earthR;
        const float3 samplePos = float3(pos.x, sphericalY, pos.z);
        const float distFade = exp(-t * 0.00014);
        float hFrac = 0.0;
        const float dens = cloudDensityAt(gCloudNoise, samplePos, bottom, top, gAtm.cloudCoverage, gAtm.cloudDetail, distFade, windOff, hFrac);
        if (dens > 1e-4) {
            float dummyH = 0.0;
            float lightTau = 0.0;
            lightTau += cloudDensityAt(gCloudNoise, samplePos + sunDir * 180.0, bottom, top, gAtm.cloudCoverage, 0.0, distFade, windOff, dummyH) * 180.0;
            lightTau += cloudDensityAt(gCloudNoise, samplePos + sunDir * 640.0, bottom, top, gAtm.cloudCoverage, 0.0, distFade, windOff, dummyH) * 460.0;
            const float opticalDepth = lightTau * extCoeff;
            float3 multiScatter = 0.0;
            float a = 1.0;
            float b = 1.0;
            float c = 1.0;
            [unroll]
            for (int oct = 0; oct < 3; ++oct) {
                const float phase = lerp(cloudHg(cosTheta, 0.65 * c), cloudHg(cosTheta, -0.3 * c), 0.35);
                const float beer = exp(-opticalDepth * a);
                multiScatter += sunColor * beer * phase * b;
                a *= 0.38;
                b *= 0.58;
                c *= 0.50;
            }
            const float powder = 1.0 - 0.45 * exp(-dens * extCoeff * stepSize * 3.5);
            const float3 amb = lerp(ambientBot, ambientTop, hFrac) * (0.45 + 0.65 * hFrac);
            const float3 stepLight = multiScatter * powder * 1.45 + amb;
            const float stepExt = dens * extCoeff;
            const float stepTrans = exp(-stepExt * stepSize);
            scattered += transmittance * stepLight * (1.0 - stepTrans);
            transmittance *= stepTrans;
        }
    }
    const float distHaze = exp(-t0 * 0.000065);
    const float horizonFade = smoothstep(0.0, 0.08, rd.y) * distHaze;
    gCloud[dtid.xy] = float4(scattered * horizonFade, lerp(1.0, transmittance, horizonFade));
}
