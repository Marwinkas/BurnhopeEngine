// Карты солнца. Ожидает gShadow0 и gFrame. Лампы и туман этот файл не считают.
// Глубина reverse-Z: ближе к солнцу значение больше. Пустой тексель 0 — свет.

float shadowTexelLit(int2 sp, int y0, float receiver, float bias) {
    sp.x = clamp(sp.x, 0, 4095);
    sp.y = clamp(sp.y, y0, y0 + 4095);
    const float mapDepth = gShadow0.Load(int3(sp, 0));
    if (mapDepth <= 1e-6) {
        return 1.0;
    }
    return (receiver + bias) >= mapDepth ? 1.0 : 0.0;
}

float shadowBilinearLit(float2 coord, int y0, float receiver, float bias) {
    const int2 ip = int2(floor(coord));
    const float2 f = frac(coord);
    const float s00 = shadowTexelLit(int2(ip.x,     ip.y + y0),     y0, receiver, bias);
    const float s10 = shadowTexelLit(int2(ip.x + 1, ip.y + y0),     y0, receiver, bias);
    const float s01 = shadowTexelLit(int2(ip.x,     ip.y + 1 + y0), y0, receiver, bias);
    const float s11 = shadowTexelLit(int2(ip.x + 1, ip.y + 1 + y0), y0, receiver, bias);
    return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
}

float cascadeOutside(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    return max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
}

float cascadeShadow(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    const float outside = max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
    if (outside > 0.0) {
        return 1.0;
    }
    const float receiver = sunClip.z / sunW;
    if (receiver <= 0.0 || receiver >= 1.0) {
        return 1.0;
    }
    const float extent = max(gSun.sunSplit[cascade], 1.0);
    const float bias = 0.05 / (extent * 8.0);
    const int y0 = int(cascade) * 4096;
    const float2 coord = saturate(sunLocal) * 4096.0 - 0.5;

    // 3x3 Bilinear Tent Filter (equivalent to 4x4 smooth PCF, 100% noise-free and rotation-invariant)
    const float w[3] = {0.25, 0.5, 0.25};
    float acc = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = w[x + 1] * w[y + 1];
            acc += shadowBilinearLit(coord + float2(float(x), float(y)), y0, receiver, bias) * weight;
        }
    }
    return acc;
}

float cascadeShadowCover(uint cascade, float3 world) {
    const float outDist = cascadeOutside(cascade, world);
    if (outDist > 0.0) {
        return 1.0;
    }
    const float fade = saturate(-outDist / 0.08);
    return lerp(1.0, cascadeShadow(cascade, world), fade);
}

float cascadeLit(uint cascade, float3 world) {
    if (cascadeOutside(cascade, world) > 0.0) {
        return 1.0;
    }
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = saturate(sunClip.xy / sunW * 0.5 + 0.5);
    const float receiver = sunClip.z / sunW;
    const float bias = 0.05 / (max(gSun.sunSplit[cascade], 1.0) * 8.0);
    const int y0 = int(cascade) * 4096;
    const int2 sp = int2(sunLocal * 4096.0) + int2(0, y0);
    return shadowTexelLit(sp, y0, receiver, bias);
}

float shadowSkyReach(uint cascade, float3 P, float3 N, uint2 px) {
    const float3 L = normalize(gSun.sunDirIntensity.xyz);
    float3 side = cross(L, float3(0.0, 1.0, 0.0));
    if (dot(side, side) < 1e-4) {
        side = cross(L, float3(1.0, 0.0, 0.0));
    }
    side = normalize(side);
    const float3 bin = normalize(cross(L, side));
    const float radii[10] = {0.3, 0.5, 0.8, 1.2, 1.8, 2.6, 3.8, 5.4, 7.6, 10.5};
    const float spin = float((px.x * 3u + px.y * 5u) & 7u) * 0.098175;
    float sum = 0.0;
    [unroll]
    for (uint k = 0u; k < 8u; ++k) {
        const float ang = spin + float(k) * 0.78539816;
        const float3 dir = side * cos(ang) + bin * sin(ang);
        float dist = 13.0;
        [unroll]
        for (uint ring = 0u; ring < 10u; ++ring) {
            const float3 q = P + dir * radii[ring] + N * 0.15;
            float lit = 1.0;
            [unroll]
            for (uint c = cascade; c < 3u; ++c) {
                if (cascadeOutside(c, q) <= 0.0) {
                    lit = cascadeLit(c, q);
                    break;
                }
            }
            if (lit > 0.5) {
                dist = radii[ring];
                break;
            }
        }
        sum += exp(-dist * 0.55);
    }
    return clamp(sum * 0.125, 0.15, 1.0);
}

float cascadeFogLit(float3 world) {
    [unroll]
    for (uint c = 0u; c < 3u; ++c) {
        if (cascadeOutside(c, world) <= 0.0) {
            return cascadeLit(c, world);
        }
    }
    return 1.0;
}