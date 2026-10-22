// Плотность кучевого слоя. Шум 128³ периодический, фаза сдвигается погодой,
// а у ребра тайла берётся вторая выборка, чтобы куб не повторялся квадратом.

float2 cloudHash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float cloudGrad2D(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const float a = dot(cloudHash22(i) * 2.0 - 1.0, f);
    const float b = dot(cloudHash22(i + float2(1.0, 0.0)) * 2.0 - 1.0, f - float2(1.0, 0.0));
    const float c = dot(cloudHash22(i + float2(0.0, 1.0)) * 2.0 - 1.0, f - float2(0.0, 1.0));
    const float d = dot(cloudHash22(i + float2(1.0, 1.0)) * 2.0 - 1.0, f - float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 0.5 + 0.5;
}

float2 cloudWeather(float2 xz, float coverage) {
    float2 q = xz * 0.00011;
    const float2 warp = float2(
        cloudGrad2D(q * 0.72 + float2(1.7, 9.2)) - 0.5,
        cloudGrad2D(q * 0.72 + float2(8.3, 2.8)) - 0.5
    );
    q += warp * 1.8;
    const float n0 = cloudGrad2D(q * 0.48);
    const float n1 = cloudGrad2D(q * 1.17 + float2(4.1, 2.3));
    const float n2 = cloudGrad2D(q * 2.63 - float2(7.4, 5.8));
    const float n3 = cloudGrad2D(q * 5.91 + float2(2.9, 8.1));
    const float macro = n0 * 0.46 + n1 * 0.28 + n2 * 0.16 + n3 * 0.10;
    const float covThresh = 1.0 - saturate(coverage * 1.12);
    const float mask = smoothstep(covThresh - 0.06, covThresh + 0.22, macro);
    const float tower = clamp(smoothstep(covThresh, covThresh + 0.32, n0 * 0.6 + n1 * 0.4), 0.26, 1.0);
    return float2(mask, tower);
}

float cloudRaw128(StructuredBuffer<float> noise, float3 uvw) {
    const float3 p = frac(uvw) * 128.0 - 0.5;
    const int3 i0 = int3(floor(p));
    const float3 f = frac(p);
    const int3 c0 = i0 & 127;
    const int3 c1 = (i0 + 1) & 127;
    const float v000 = noise[c0.x + c0.y * 128 + c0.z * 16384];
    const float v100 = noise[c1.x + c0.y * 128 + c0.z * 16384];
    const float v010 = noise[c0.x + c1.y * 128 + c0.z * 16384];
    const float v110 = noise[c1.x + c1.y * 128 + c0.z * 16384];
    const float v001 = noise[c0.x + c0.y * 128 + c1.z * 16384];
    const float v101 = noise[c1.x + c0.y * 128 + c1.z * 16384];
    const float v011 = noise[c0.x + c1.y * 128 + c1.z * 16384];
    const float v111 = noise[c1.x + c1.y * 128 + c1.z * 16384];
    const float x00 = lerp(v000, v100, f.x);
    const float x10 = lerp(v010, v110, f.x);
    const float x01 = lerp(v001, v101, f.x);
    const float x11 = lerp(v011, v111, f.x);
    return lerp(lerp(x00, x10, f.y), lerp(x01, x11, f.y), f.z);
}

float cloudRaw32(StructuredBuffer<float> noise, float3 uvw) {
    const uint base = 128u * 128u * 128u;
    const float3 p = frac(uvw) * 32.0 - 0.5;
    const int3 i0 = int3(floor(p));
    const float3 f = frac(p);
    const int3 c0 = i0 & 31;
    const int3 c1 = (i0 + 1) & 31;
    const float v000 = noise[base + c0.x + c0.y * 32 + c0.z * 1024];
    const float v100 = noise[base + c1.x + c0.y * 32 + c0.z * 1024];
    const float v010 = noise[base + c0.x + c1.y * 32 + c0.z * 1024];
    const float v110 = noise[base + c1.x + c1.y * 32 + c0.z * 1024];
    const float v001 = noise[base + c0.x + c0.y * 32 + c1.z * 1024];
    const float v101 = noise[base + c1.x + c0.y * 32 + c1.z * 1024];
    const float v011 = noise[base + c0.x + c1.y * 32 + c1.z * 1024];
    const float v111 = noise[base + c1.x + c1.y * 32 + c1.z * 1024];
    const float x00 = lerp(v000, v100, f.x);
    const float x10 = lerp(v010, v110, f.x);
    const float x01 = lerp(v001, v101, f.x);
    const float x11 = lerp(v011, v111, f.x);
    return lerp(lerp(x00, x10, f.y), lerp(x01, x11, f.y), f.z);
}

float cloudSeam(StructuredBuffer<float> noise, float3 uvw, bool detail) {
    const float3 f = frac(uvw);
    const float3 edge = min(f, 1.0 - f);
    const float seam = saturate(min(edge.x, min(edge.y, edge.z)) * 5.0);
    const float3 shift = float3(0.37, 0.21, 0.58);
    if (detail) {
        return lerp(cloudRaw32(noise, uvw + shift), cloudRaw32(noise, uvw), seam);
    }
    return lerp(cloudRaw128(noise, uvw + shift), cloudRaw128(noise, uvw), seam);
}

float cloudDensityAt(
    StructuredBuffer<float> noise,
    float3 pos,
    float bottom,
    float top,
    float coverage,
    float detailAmt,
    float distFade,
    float2 windOff,
    out float hFracOut)
{
    const float layer = max(top - bottom, 1000.0);
    const float rawH = (pos.y - bottom) / layer;
    hFracOut = saturate(rawH);
    if (rawH <= 0.0 || rawH >= 1.0) {
        return 0.0;
    }
    const float2 worldXZ = pos.xz + windOff + float2(rawH * 280.0, rawH * 90.0);
    const float2 weather = cloudWeather(worldXZ, coverage);
    if (weather.x <= 1e-3 || rawH >= weather.y) {
        return 0.0;
    }
    const float localH = saturate(rawH / max(weather.y, 0.2));
    const float heightProfile = smoothstep(0.0, 0.14, localH) * smoothstep(1.0, 0.45, localH);
    const float2 phase = float2(
        cloudGrad2D(worldXZ * 0.000041 + float2(2.2, 7.4)),
        cloudGrad2D(worldXZ * 0.000033 + float2(9.1, 1.6))
    );
    const float3 p3 = float3(worldXZ.x, pos.y, worldXZ.y) + float3(phase.x, phase.y * 0.35, phase.y) * 4200.0;
    const float3 pRot = float3(p3.x * 0.8 - p3.z * 0.6, p3.y, p3.x * 0.6 + p3.z * 0.8);
    const float billow = cloudSeam(noise, p3 * 0.00024, false) * 0.65
        + cloudSeam(noise, pRot * 0.00059 + float3(0.41, 0.19, 0.73), false) * 0.35;
    float baseCloud = saturate((weather.x * heightProfile - (1.0 - billow) * 0.58) / 0.42);
    if (baseCloud <= 1e-4) {
        return 0.0;
    }
    const float detailLod = saturate(detailAmt) * distFade;
    if (detailLod > 0.01) {
        const float detail = cloudSeam(noise, p3 * 0.0014 - float3(windOff.x, 0.0, windOff.y) * 0.25, true);
        const float fluffy = lerp(detail, 1.0 - detail, saturate(localH * 1.4));
        const float erode = fluffy * 0.38 * detailLod * (1.0 - baseCloud * 0.6);
        baseCloud = saturate((baseCloud - erode) / max(1.0 - erode, 0.15));
    }
    return baseCloud * lerp(0.5, 1.35, localH);
}
