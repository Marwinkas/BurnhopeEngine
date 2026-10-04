// Облака в половине кадра. Шахматка пишет половину текселей, апскейл читает глубину.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(44, 0)]] RWTexture2D<float4> gCloud;
[[vk::binding(45, 0)]] Texture2D<float4> gCloudRead;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(46, 0)]] StructuredBuffer<float> gNoise;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

float noiseTex(float3 uv, uint res, uint base) {
    const float3 q = frac(uv) * float(res);
    const uint3 i = uint3(q) % res;
    return gNoise[base + i.x + i.y * res + i.z * res * res];
}

float cloudDensityAt(float3 world, float height01) {
    const float3 wind = float3(gAtm.cloudSpeedX, height01 * 8.0, gAtm.cloudSpeedZ) * gAtm.time;
    const float3 p = world + wind;
    const float shape = 1.0 - noiseTex(p / 2200.0, 128u, 0u);
    const float detail = noiseTex(p / 380.0 + 19.0, 32u, 128u * 128u * 128u);
    const float base = shape - (1.0 - gAtm.cloudCoverage) * 0.55;
    const float eroded = base - detail * gAtm.cloudDetail * (0.35 + height01);
    return saturate(eroded) * gAtm.cloudDensity * 8.0;
}

float cloudLight(float3 world, float3 sun, float bottom, float top) {
    float optical = 0.0;
    const float step = max((top - bottom) / 6.0, 40.0);
    for (int i = 1; i <= 4; i++) {
        const float3 p = world + sun * step * float(i);
        const float h = length(float3(p.x, p.y + kAtmRg, p.z));
        const float altitude = h - kAtmRg;
        if (altitude < bottom || altitude > top) {
            break;
        }
        const float u = saturate((altitude - bottom) / max(top - bottom, 1.0));
        optical += cloudDensityAt(p, u) * step * 0.001;
    }
    const float beer = exp(-optical);
    const float powder = 1.0 - exp(-optical * 2.0);
    return beer * lerp(1.0, powder, 0.6);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudMarch(uint3 dtid : SV_DispatchThreadID) {
    const uint width = max(gFrame.extent.x / 2u, 1u);
    const uint height = max(gFrame.extent.y / 2u, 1u);
    if (dtid.x >= width || dtid.y >= height) {
        return;
    }
    const uint parity = gAtm.parity > 0.5 ? 1u : 0u;
    if (gAtm.time > 0.05 && ((dtid.x + dtid.y) & 1u) != parity) {
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(float(width), float(height));
    const float3 farP = reconstructWorld(uv, 0.0);
    const float3 rd = normalize(farP - gFrame.cameraPos.xyz);
    const float3 ro = float3(gFrame.cameraPos.x, gFrame.cameraPos.y + kAtmRg, gFrame.cameraPos.z);
    const float bottom = kAtmRg + gAtm.cloudBottom;
    const float top = kAtmRg + max(gAtm.cloudTop, gAtm.cloudBottom + 200.0);
    const float tIn = atmExit(ro, rd);
    float t0 = 1e20;
    float t1 = -1.0;
    const float b0 = dot(ro, rd);
    const float h0 = b0 * b0 - (dot(ro, ro) - bottom * bottom);
    const float h1 = b0 * b0 - (dot(ro, ro) - top * top);
    if (h0 >= 0.0) {
        const float s = sqrt(h0);
        t0 = min(t0, max(-b0 - s, 0.0));
        t1 = max(t1, -b0 + s);
    }
    if (h1 >= 0.0) {
        const float s = sqrt(h1);
        t0 = min(t0, max(-b0 - s, 0.0));
        t1 = max(t1, -b0 + s);
    }
    if (t1 < t0 || rd.y < -0.02) {
        gCloud[dtid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    t1 = min(t1, tIn);
    const float3 sun = atmSunDir(gAtm);
    const float cosT = dot(rd, sun);
    const float hg = atmMiePhase(cosT, 0.55);
    float3 lum = float3(0.0, 0.0, 0.0);
    float trans = 1.0;
    const int steps = 20;
    const float dt = max((t1 - t0) / float(steps), 30.0);
    for (int i = 0; i < steps; i++) {
        const float t = t0 + dt * (float(i) + 0.5);
        if (t > t1 || trans < 0.02) {
            break;
        }
        const float3 p = gFrame.cameraPos.xyz + rd * t;
        const float altitude = length(float3(p.x, p.y + kAtmRg, p.z)) - kAtmRg;
        const float u = saturate((altitude - gAtm.cloudBottom) / max(gAtm.cloudTop - gAtm.cloudBottom, 1.0));
        const float dens = cloudDensityAt(p, u);
        if (dens <= 0.0001) {
            continue;
        }
        const float light = cloudLight(p, sun, gAtm.cloudBottom, gAtm.cloudTop);
        const float3 sunCol = atmSunIllum(gAtm, sun) * light * hg * 0.15;
        const float3 amb = float3(0.25, 0.35, 0.55) * (0.15 + u) * gAtm.nightAmbient * 4.0 + sunCol;
        const float beer = exp(-dens * dt * 0.004);
        lum += amb * dens * trans * dt * 0.002 * atmViewScale();
        trans *= beer;
    }
    gCloud[dtid.xy] = float4(lum, trans);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudUp(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y || gFrame.debugMode != 0u) {
        return;
    }
    const uint vis = gVis.Load(int3(dtid.xy, 0));
    const float depth = gDepth.Load(int3(dtid.xy, 0));
    const float2 uv = (float2(dtid.xy) + 0.5) * gFrame.invExtent;
    float4 acc = float4(0.0, 0.0, 0.0, 0.0);
    float wsum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            const float2 s = uv + float2(float(x), float(y)) * gFrame.invExtent;
            const uint2 sp = uint2(s * gFrame.extent.xy);
            const float nd = gDepth.Load(int3(int2(sp), 0));
            const float w = exp(-abs(nd - depth) * 40.0);
            acc += gCloudRead.SampleLevel(gClamp, s, 0.0) * w;
            wsum += w;
        }
    }
    const float4 cloud = acc / max(wsum, 1e-4);
    if ((gPass.passMask & 2048u) == 0u) {
        return;
    }
    if (vis != 0u) {
        if (depth <= 0.0) {
            return;
        }
        const float3 surface = reconstructWorld(dtid.xy, depth);
        const float surfaceDist = length(surface - gFrame.cameraPos.xyz);
        if (surfaceDist < gAtm.cloudBottom * 0.85) {
            return;
        }
    }
    if (cloud.a <= 0.001 && dot(cloud.rgb, cloud.rgb) <= 1e-6) {
        return;
    }
    const float3 src = gHdr[dtid.xy].rgb;
    gHdr[dtid.xy] = float4(cloud.rgb + src * saturate(cloud.a), 1.0);
}
