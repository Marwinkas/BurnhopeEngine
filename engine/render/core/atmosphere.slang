// Рэлей, Ми, озон. Дескрипторов нет: текстуру пропускания передаёт вызывающий.

static const float kAtmRg = 6360000.0;
static const float kAtmRt = 6460000.0;
static const float kAtmHr = 8000.0;
static const float kAtmHm = 1200.0;

struct AtmosphereParams {
    float sunZenith;
    float sunAzimuth;
    float sunIntensity;
    float sunDisk;
    float rayleigh;
    float mie;
    float mieG;
    float ozone;
    float cloudCoverage;
    float cloudDensity;
    float cloudBottom;
    float cloudTop;
    float cloudSpeedX;
    float cloudSpeedZ;
    float cloudDetail;
    float moon;
    float stars;
    float nightAmbient;
    float time;
    float parity;
    float reserved0;
    float reserved1;
    float z0; float z1; float z2; float z3;
    float z4; float z5; float z6; float z7;
    float z8; float z9; float z10; float z11;
    float z12; float z13; float z14; float z15;
    float z16; float z17; float z18; float z19;
    float z20; float z21; float z22; float z23;
    float z24; float z25;
};

float3 atmBetaR(AtmosphereParams a) {
    return float3(5.802e-6, 13.558e-6, 33.100e-6) * a.rayleigh;
}

float3 atmBetaM(AtmosphereParams a) {
    return float3(3.996e-6, 3.996e-6, 3.996e-6) * a.mie;
}

float3 atmBetaO(AtmosphereParams a) {
    return float3(0.650e-6, 1.881e-6, 0.085e-6) * a.ozone;
}

float3 atmSunDir(AtmosphereParams a) {
    const float elev = a.sunZenith * 0.017453292;
    const float az = a.sunAzimuth * 0.017453292;
    const float c = cos(elev);
    return normalize(float3(c * cos(az), sin(elev), c * sin(az)));
}

float atmSunFade(float3 sun) {
    return saturate((sun.y + 0.02) / 0.07);
}

float atmRayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * kPi)) * (1.0 + cosTheta * cosTheta);
}

float atmMiePhase(float cosTheta, float g) {
    const float g2 = g * g;
    const float num = (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    const float den = (2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5);
    return (3.0 / (8.0 * kPi)) * num / den;
}

float atmOzone(float height) {
    const float km = height / 1000.0;
    return saturate(1.0 - abs(km - 25.0) / 15.0);
}

float atmExit(float3 ro, float3 rd) {
    const float b = dot(ro, rd);
    const float c = dot(ro, ro) - kAtmRt * kAtmRt;
    return -b + sqrt(max(b * b - c, 0.0));
}

float atmGround(float3 ro, float3 rd) {
    const float b = dot(ro, rd);
    const float c = dot(ro, ro) - kAtmRg * kAtmRg;
    const float d = b * b - c;
    if (d < 0.0) {
        return 1e20;
    }
    const float t = -b - sqrt(d);
    return t > 0.0 ? t : 1e20;
}

float3 atmOptical(AtmosphereParams a, float3 ro, float3 rd, float tMax) {
    const int steps = 16;
    const float dt = tMax / float(steps);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < steps; i++) {
        const float3 p = ro + rd * (dt * (float(i) + 0.5));
        const float h = max(length(p) - kAtmRg, 0.0);
        sum += float3(exp(-h / kAtmHr), exp(-h / kAtmHm), atmOzone(h)) * dt;
    }
    return sum;
}

float3 atmTransmittanceRay(AtmosphereParams a, float3 ro, float3 rd) {
    const float tGround = atmGround(ro, rd);
    float tMax = atmExit(ro, rd);
    if (tGround < tMax) {
        tMax = tGround;
    }
    if (tMax < 1.0) {
        return float3(0.0, 0.0, 0.0);
    }
    const float3 depth = atmOptical(a, ro, rd, tMax);
    return exp(-(atmBetaR(a) * depth.x + atmBetaM(a) * depth.y + atmBetaO(a) * depth.z));
}

float2 atmTransUv(float3 pos, float3 dir) {
    const float h = saturate((length(pos) - kAtmRg) / (kAtmRt - kAtmRg));
    const float mu = dot(normalize(pos), dir) * 0.5 + 0.5;
    return float2(mu, h);
}

float3 atmSampleTrans(Texture2D<float4> tex, SamplerState samp, float3 pos, float3 dir) {
    return tex.SampleLevel(samp, atmTransUv(pos, dir), 0.0).rgb;
}

float2 atmSkyUv(float3 rd) {
    const float mu = saturate(rd.y * 0.5 + 0.5);
    const float phi = atan2(rd.z, rd.x) * 0.15915494 + 0.5;
    return float2(phi, mu);
}

float3 atmSkyDir(float2 uv) {
    const float phi = (uv.x * 2.0 - 1.0) * kPi;
    const float mu = uv.y * 2.0 - 1.0;
    const float horiz = sqrt(max(1.0 - mu * mu, 0.0));
    return normalize(float3(cos(phi) * horiz, mu, sin(phi) * horiz));
}

float3 atmSunIllum(AtmosphereParams a, float3 sun) {
    const float day = atmSunFade(sun);
    const float warm = saturate(1.0 - sun.y * 3.0);
    const float3 color = lerp(float3(1.0, 0.97, 0.92), float3(1.0, 0.35, 0.12), warm * (1.0 - day * 0.5));
    return color * (a.sunIntensity / 25000.0) * max(day, 0.02);
}
