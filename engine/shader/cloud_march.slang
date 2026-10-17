// Объёмный реймарчинг кучевых облаков (Nubis / Horizon / RDR2).

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(44, 0)]] RWTexture2D<float4> gCloud;
[[vk::binding(46, 0)]] StructuredBuffer<float> gCloudNoise;

// Трилинейная выборка из 3D буфера 128^3
float sampleBase3D(float3 uvw) {
    const float3 p = frac(uvw) * 128.0 - 0.5;
    const int3 i0 = int3(floor(p));
    const float3 f = frac(p);
    const int3 c0 = i0 & 127;
    const int3 c1 = (i0 + 1) & 127;

    const float v000 = gCloudNoise[c0.x + c0.y * 128 + c0.z * 16384];
    const float v100 = gCloudNoise[c1.x + c0.y * 128 + c0.z * 16384];
    const float v010 = gCloudNoise[c0.x + c1.y * 128 + c0.z * 16384];
    const float v110 = gCloudNoise[c1.x + c1.y * 128 + c0.z * 16384];
    const float v001 = gCloudNoise[c0.x + c0.y * 128 + c1.z * 16384];
    const float v101 = gCloudNoise[c1.x + c0.y * 128 + c1.z * 16384];
    const float v011 = gCloudNoise[c0.x + c1.y * 128 + c1.z * 16384];
    const float v111 = gCloudNoise[c1.x + c1.y * 128 + c1.z * 16384];

    const float x00 = lerp(v000, v100, f.x);
    const float x10 = lerp(v010, v110, f.x);
    const float x01 = lerp(v001, v101, f.x);
    const float x11 = lerp(v011, v111, f.x);
    const float y0 = lerp(x00, x10, f.y);
    const float y1 = lerp(x01, x11, f.y);
    return lerp(y0, y1, f.z);
}

// Трилинейная выборка из 3D буфера деталей 32^3
float sampleDetail3D(float3 uvw) {
    const uint base = 128u * 128u * 128u;
    const float3 p = frac(uvw) * 32.0 - 0.5;
    const int3 i0 = int3(floor(p));
    const float3 f = frac(p);
    const int3 c0 = i0 & 31;
    const int3 c1 = (i0 + 1) & 31;

    const float v000 = gCloudNoise[base + c0.x + c0.y * 32 + c0.z * 1024];
    const float v100 = gCloudNoise[base + c1.x + c0.y * 32 + c0.z * 1024];
    const float v010 = gCloudNoise[base + c0.x + c1.y * 32 + c0.z * 1024];
    const float v110 = gCloudNoise[base + c1.x + c1.y * 32 + c0.z * 1024];
    const float v001 = gCloudNoise[base + c0.x + c0.y * 32 + c1.z * 1024];
    const float v101 = gCloudNoise[base + c1.x + c0.y * 32 + c1.z * 1024];
    const float v011 = gCloudNoise[base + c0.x + c1.y * 32 + c1.z * 1024];
    const float v111 = gCloudNoise[base + c1.x + c1.y * 32 + c1.z * 1024];

    const float x00 = lerp(v000, v100, f.x);
    const float x10 = lerp(v010, v110, f.x);
    const float x01 = lerp(v001, v101, f.x);
    const float x11 = lerp(v011, v111, f.x);
    const float y0 = lerp(x00, x10, f.y);
    const float y1 = lerp(x01, x11, f.y);
    return lerp(y0, y1, f.z);
}

// Неповторяющийся 2D-шум для бесконечной генерации уникальных облачных фронтов
float2 weatherHash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy) * 2.0 - 1.0;
}

float weatherGrad2D(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    const float a = dot(weatherHash22(i + float2(0.0, 0.0)), f - float2(0.0, 0.0));
    const float b = dot(weatherHash22(i + float2(1.0, 0.0)), f - float2(1.0, 0.0));
    const float c = dot(weatherHash22(i + float2(0.0, 1.0)), f - float2(0.0, 1.0));
    const float d = dot(weatherHash22(i + float2(1.0, 1.0)), f - float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y) * 0.5 + 0.5;
}

// Возвращает x = покрытие облаками в данной точке, y = локальную высоту башни облака (0.3 .. 1.0)
float2 sampleWeatherField(float2 xz, float coverage) {
    float2 q = xz * 0.00014;
    // Domain warping: закручивает гряды облаков так, чтобы они никогда не образовывали сетку
    const float2 warp = float2(
        weatherGrad2D(q * 0.8 + float2(1.7, 9.2)) - 0.5,
        weatherGrad2D(q * 0.8 + float2(8.3, 2.8)) - 0.5
    );
    q += warp * 1.35;

    const float n0 = weatherGrad2D(q * 0.55);
    const float n1 = weatherGrad2D(q * 1.35 + float2(4.1, 2.3));
    const float n2 = weatherGrad2D(q * 3.10 - float2(7.4, 5.8));
    const float n3 = weatherGrad2D(q * 6.70 + float2(2.9, 8.1));

    const float macro = n0 * 0.45 + n1 * 0.30 + n2 * 0.16 + n3 * 0.09;
    const float covThresh = 1.0 - saturate(coverage * 1.15);
    const float cloudMask = smoothstep(covThresh - 0.08, covThresh + 0.28, macro);

    // Крупные облака поднимаются высокими кучевыми башнями, мелкие остаются низкими
    const float towerHeight = clamp(smoothstep(covThresh, covThresh + 0.35, n0 * 0.6 + n1 * 0.4), 0.28, 1.0);
    return float2(cloudMask, towerHeight);
}

float hgPhase(float cosTheta, float g) {
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * kPi * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

float cloudDensityAt(float3 pos, float bottom, float top, float coverage, float detailAmt, float distFade, out float hFracOut) {
    const float layerThickness = max(top - bottom, 1000.0);
    const float rawH = (pos.y - bottom) / layerThickness;
    hFracOut = saturate(rawH);
    if (rawH <= 0.0 || rawH >= 1.0) {
        return 0.0;
    }

    const float2 wind = float2(gAtm.cloudSpeedX, gAtm.cloudSpeedZ) * gAtm.time;
    // Сдвиг верхушек облаков по ветру (wind shear)
    const float2 worldXZ = pos.xz + wind + float2(rawH * 280.0, rawH * 90.0);

    const float2 weather = sampleWeatherField(worldXZ, coverage);
    if (weather.x <= 1e-3 || rawH >= weather.y) {
        return 0.0;
    }

    const float localH = saturate(rawH / max(weather.y, 0.2));
    // Плоское основание кучевого облака и округлая пышная шапка
    const float bottomRound = smoothstep(0.0, 0.14, localH);
    const float topRound = smoothstep(1.0, 0.45, localH);
    const float heightProfile = bottomRound * topRound;

    // Поворот координат между октавами 3D-шума убирает любое ощущение повторения
    const float3 p3 = float3(worldXZ.x, pos.y, worldXZ.y);
    const float3 pRot = float3(p3.x * 0.8 - p3.z * 0.6, p3.y, p3.x * 0.6 + p3.z * 0.8);
    const float billow0 = sampleBase3D(p3 * 0.00024);
    const float billow1 = sampleBase3D(pRot * 0.00059 + float3(0.41, 0.19, 0.73));
    const float billow = billow0 * 0.65 + billow1 * 0.35;

    float baseCloud = saturate((weather.x * heightProfile - (1.0 - billow) * 0.58) / 0.42);
    if (baseCloud <= 1e-4) {
        return 0.0;
    }

    // Высокочастотная эрозия краёв (плавно выключается вдали через distFade, чтобы не было шипения на горизонте)
    const float detailLod = saturate(detailAmt) * distFade;
    if (detailLod > 0.01) {
        const float detail = sampleDetail3D(p3 * 0.0014 - float3(wind.x, 0.0, wind.y) * 0.25);
        const float fluffyEdge = lerp(detail, 1.0 - detail, saturate(localH * 1.4));
        const float erode = fluffyEdge * 0.38 * detailLod * (1.0 - baseCloud * 0.6);
        baseCloud = saturate((baseCloud - erode) / max(1.0 - erode, 0.15));
    }

    // Плотность растёт к верхушке кучевого облака
    return baseCloud * lerp(0.5, 1.35, localH);
}

// Пересечение луча со сферическим слоем атмосферы (убирает эффект плоского потолка)
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

    const float3 sunDir = atmSunDir();
    const float cosTheta = dot(rd, sunDir);
    const float sunElev = saturate(sunDir.y * 1.8 + 0.08);

    // Яркий солнечный цвет (золотой на закате, ослепительно бело-кремовый днём)
    const float3 sunColor = lerp(float3(1.0, 0.45, 0.18), float3(1.0, 0.98, 0.94), saturate(sunDir.y * 2.2)) * (2.6 * sunElev + 0.15);
    // Глубокий небесно-голубой амбиент сверху и тёплый дымчатый снизу
    const float3 ambientTop = lerp(float3(0.28, 0.24, 0.36), float3(0.52, 0.68, 0.92), sunElev);
    const float3 ambientBot = lerp(float3(0.14, 0.12, 0.20), float3(0.26, 0.34, 0.48), sunElev);

    const int steps = 48;
    const float marchSpan = min(t1 - t0, 16000.0);
    const float stepSize = marchSpan / float(steps);
    // Мягкий джиттер, который уменьшается на горизонте (никакого шипения вдали)
    const float distDitherFade = saturate(1.0 - t0 / 18000.0);
    const float dither = (frac(52.9829189 * frac(0.06711056 * float(dtid.x) + 0.00583715 * float(dtid.y))) - 0.5) * 0.35 * distDitherFade;

    const float extCoeff = gAtm.cloudDensity * 0.024;
    const float earthR = 320000.0;
    const float3 planetCenter = float3(gFrame.cameraPos.x, -earthR, gFrame.cameraPos.z);

    float transmittance = 1.0;
    float3 scattered = 0.0;

    [loop]
    for (int i = 0; i < steps; ++i) {
        if (transmittance < 0.01) {
            break;
        }
        const float t = t0 + (float(i) + 0.5 + dither) * stepSize;
        const float3 pos = gFrame.cameraPos.xyz + rd * t;
        // Сферическая высота над поверхностью планеты
        const float sphericalY = length(pos - planetCenter) - earthR;
        const float3 samplePos = float3(pos.x, sphericalY, pos.z);
        const float distFade = exp(-t * 0.00014);

        float hFrac = 0.0;
        const float dens = cloudDensityAt(samplePos, bottom, top, gAtm.cloudCoverage, gAtm.cloudDetail, distFade, hFrac);
        if (dens > 1e-4) {
            // Конус солнечного света (3 шага к солнцу)
            float dummyH = 0.0;
            float lightTau = 0.0;
            lightTau += cloudDensityAt(samplePos + sunDir * 110.0, bottom, top, gAtm.cloudCoverage, gAtm.cloudDetail * 0.5, distFade, dummyH) * 110.0;
            lightTau += cloudDensityAt(samplePos + sunDir * 320.0, bottom, top, gAtm.cloudCoverage, 0.0, distFade, dummyH) * 210.0;
            lightTau += cloudDensityAt(samplePos + sunDir * 750.0, bottom, top, gAtm.cloudCoverage, 0.0, distFade, dummyH) * 430.0;
            const float opticalDepth = lightTau * extCoeff;

            // 3-октавное многократное рассеивание (Wrenninge / Frostbite) — делает облака белыми и пушистыми, а не серыми
            float3 multiScatter = 0.0;
            float a = 1.0;
            float b = 1.0;
            float c = 1.0;
            [unroll]
            for (int oct = 0; oct < 3; ++oct) {
                const float phase = lerp(hgPhase(cosTheta, 0.65 * c), hgPhase(cosTheta, -0.3 * c), 0.35);
                const float beer = exp(-opticalDepth * a);
                multiScatter += sunColor * beer * phase * b;
                a *= 0.38;
                b *= 0.58;
                c *= 0.50;
            }

            // Эффект пудры (подчёркивает пушистые освещённые рельефы на краях клубов)
            const float powder = 1.0 - 0.45 * exp(-dens * extCoeff * stepSize * 3.5);
            const float3 amb = lerp(ambientBot, ambientTop, hFrac) * (0.45 + 0.65 * hFrac);
            const float3 stepLight = multiScatter * powder * 1.45 + amb;

            const float stepExt = dens * extCoeff;
            const float stepTrans = exp(-stepExt * stepSize);
            scattered += transmittance * stepLight * (1.0 - stepTrans);
            transmittance *= stepTrans;
        }
    }

    // Воздушная перспектива у горизонта: далёкие облака мягко растворяются в цвете неба
    const float distHaze = exp(-t0 * 0.000065);
    const float horizonFade = smoothstep(0.0, 0.08, rd.y) * distHaze;
    const float finalAlpha = lerp(1.0, transmittance, horizonFade);
    const float3 finalColor = scattered * horizonFade;
    gCloud[dtid.xy] = float4(finalColor, finalAlpha);
}