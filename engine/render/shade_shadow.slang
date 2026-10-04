// Карты солнца. Ожидает gShadow0 и gFrame. Лампы и туман этот файл не считают.

float cascadeShadow(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    const float outside = max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
    if (outside > 0.0 && cascade < 2u) {
        return 1.0;
    }
    const float receiver = sunClip.z / sunW;
    const float extent = max(gSun.sunSplit[cascade], 1.0);
    const float bias = 0.08 / (extent * 8.0);
    const int y0 = int(cascade) * 2048;
    const int2 basePx = int2(saturate(sunLocal) * 2048.0) + int2(0, y0);
    float litSum = 0.0;
    [unroll]
    for (int sy = -2; sy <= 2; ++sy) {
        [unroll]
        for (int sx = -2; sx <= 2; ++sx) {
            const int2 sp = int2(clamp(basePx.x + sx, 0, 2047), clamp(basePx.y + sy, y0, y0 + 2047));
            const float mapDepth = gShadow0.Load(int3(sp, 0));
            litSum += (receiver + bias) >= mapDepth ? 1.0 : 0.0;
        }
    }
    return litSum / 25.0;
}

float cascadeOutside(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    return max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
}

float cascadeLit(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = saturate(sunClip.xy / sunW * 0.5 + 0.5);
    const float receiver = sunClip.z / sunW;
    const float bias = 0.08 / (max(gSun.sunSplit[cascade], 1.0) * 8.0);
    const int y0 = int(cascade) * 2048;
    const int2 sp = int2(sunLocal * 2048.0) + int2(0, y0);
    const float mapDepth = gShadow0.Load(int3(clamp(sp, int2(0, y0), int2(2047, y0 + 2047)), 0));
    return (receiver + bias) >= mapDepth ? 1.0 : 0.35;
}
