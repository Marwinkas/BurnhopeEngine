#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;
[[vk::binding(40, 0)]] RWTexture2D<float4> gTrans;
[[vk::binding(41, 0)]] RWTexture2D<float4> gMs;
[[vk::binding(42, 0)]] RWTexture2D<float4> gSky;
[[vk::binding(43, 0)]] Texture2D<float4> gTransRead;
[[vk::binding(23, 0)]] SamplerState gClamp;

[shader("compute")]
[numthreads(8, 8, 1)]
void skyTransmittance(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= 256u || dtid.y >= 64u) {
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(256.0, 64.0);
    const float mu = uv.x * 2.0 - 1.0;
    const float h = uv.y * (kAtmRt - kAtmRg);
    const float3 pos = float3(0.0, kAtmRg + h, 0.0);
    const float horiz = sqrt(max(1.0 - mu * mu, 0.0));
    const float3 dir = float3(horiz, mu, 0.0);
    gTrans[dtid.xy] = float4(atmTransmittanceRay(gAtm, pos, dir), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void skyMulti(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= 32u || dtid.y >= 32u) {
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / 32.0;
    const float mu = uv.x * 2.0 - 1.0;
    const float nu = uv.y * 2.0 - 1.0;
    const float3 pos = float3(0.0, kAtmRg + 200.0, 0.0);
    const float horiz = sqrt(max(1.0 - mu * mu, 0.0));
    const float3 view = float3(horiz, mu, 0.0);
    const float3 sun = float3(sqrt(max(1.0 - nu * nu, 0.0)), nu, 0.0);
    const float3 through = atmSampleTrans(gTransRead, gClamp, pos, view);
    const float3 sunT = atmSampleTrans(gTransRead, gClamp, pos, sun);
    const float3 scatter = atmBetaR(gAtm) * atmRayleighPhase(dot(view, sun));
    gMs[dtid.xy] = float4((1.0 - through) * scatter * sunT * atmSunIllum(gAtm, sun), 1.0);
}

float3 atmInscatter(float3 rd, float3 sun) {
    const float3 ro = float3(0.0, kAtmRg + 200.0, 0.0);
    float tMax = min(atmExit(ro, rd), atmGround(ro, rd));
    tMax = min(tMax, 80000.0);
    if (tMax < 10.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const int steps = 16;
    const float dt = tMax / float(steps);
    const float3 illum = atmSunIllum(gAtm, sun);
    float3 lum = float3(0.0, 0.0, 0.0);
    float3 trans = float3(1.0, 1.0, 1.0);
    const float cosT = dot(rd, sun);
    const float phaseR = atmRayleighPhase(cosT);
    const float phaseM = atmMiePhase(cosT, gAtm.mieG);
    for (int i = 0; i < steps; i++) {
        const float3 p = ro + rd * (dt * (float(i) + 0.5));
        const float h = max(length(p) - kAtmRg, 0.0);
        const float densR = exp(-h / kAtmHr);
        const float densM = exp(-h / kAtmHm);
        const float3 ext = atmBetaR(gAtm) * densR + atmBetaM(gAtm) * densM + atmBetaO(gAtm) * atmOzone(h);
        const float3 stepT = exp(-ext * dt);
        const float3 sunT = atmSampleTrans(gTransRead, gClamp, p, sun);
        const float3 scatter = (atmBetaR(gAtm) * densR * phaseR + atmBetaM(gAtm) * densM * phaseM) * illum * sunT;
        lum += trans * scatter * dt;
        trans *= stepT;
    }
    return lum;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void skyView(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= 192u || dtid.y >= 108u) {
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(192.0, 108.0);
    const float3 rd = atmSkyDir(uv);
    const float3 sun = atmSunDir(gAtm);
    float3 color = atmInscatter(rd, sun) * atmViewScale();
    const float mu = rd.y * 0.5 + 0.5;
    const float nu = sun.y * 0.5 + 0.5;
    const float2 msUv = float2(mu, nu);
    const float3 ro = float3(0.0, kAtmRg + 200.0, 0.0);
    color += gMs[int2(int(msUv.x * 31.0), int(msUv.y * 31.0))].rgb * atmSampleTrans(gTransRead, gClamp, ro, rd) * atmViewScale();
    const float disk = cos(max(0.00463 * max(gAtm.sunDisk, 0.2), 0.02));
    const float sunDot = dot(rd, sun);
    if (sunDot > disk && rd.y > -0.05) {
        const float core = saturate((sunDot - disk) / max(1.0 - disk, 1e-4));
        color += atmSunIllum(gAtm, sun) * atmSampleTrans(gTransRead, gClamp, ro, sun) * (4.0 + core * 40.0);
    }
    if (sun.y < -0.02 && rd.y > 0.05) {
        const float3 cell = floor(rd * 280.0);
        const float n = frac(sin(dot(cell, float3(12.9898, 78.233, 45.164))) * 43758.5453);
        if (n > 0.997) {
            color += gAtm.stars * float3(0.7, 0.8, 1.0) * (n - 0.997) * 40.0;
        }
        const float3 moon = -sun;
        if (dot(rd, moon) > 0.9996) {
            color += float3(0.65, 0.72, 0.9) * gAtm.moon * 6.0;
        }
    }
    color += float3(0.02, 0.035, 0.08) * gAtm.nightAmbient * saturate(-sun.y * 4.0);
    gSky[dtid.xy] = float4(color, 1.0);
}
