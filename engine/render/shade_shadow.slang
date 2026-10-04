// Карты солнца. Ожидает gShadow0 и gFrame. Лампы и туман этот файл не считают.

float2 vogelDisk(int i, int n, float turn) {
    const float r = sqrt((float(i) + 0.5) / float(n));
    const float theta = 2.39996323 * float(i) + turn;
    return float2(cos(theta), sin(theta)) * r;
}

float pcssPenumbra(float receiver, float blocker) {
    if (blocker <= 1e-4 || receiver <= blocker) {
        return 1.0;
    }
    return clamp(((receiver - blocker) / blocker) * 8.0, 1.0, 12.0);
}

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
    const float turn = frac(dot(world.xz, float2(0.754877666, 0.569840296))) * 6.2831853;
    float blockerSum = 0.0;
    float blockerN = 0.0;
    [unroll]
    for (int i = 0; i < 8; ++i) {
        const float2 o = vogelDisk(i, 8, turn) * 3.0;
        const int2 sp = int2(clamp(basePx.x + int(o.x), 0, 2047), clamp(basePx.y + int(o.y), y0, y0 + 2047));
        const float mapDepth = gShadow0.Load(int3(sp, 0));
        if (mapDepth > 0.001 && mapDepth > receiver) {
            blockerSum += mapDepth;
            blockerN += 1.0;
        }
    }
    const float radius = blockerN > 0.0 ? pcssPenumbra(receiver, blockerSum / blockerN) : 1.0;
    float litSum = 0.0;
    [unroll]
    for (int s = 0; s < 12; ++s) {
        const float2 o = vogelDisk(s, 12, turn) * radius;
        const int2 sp = int2(clamp(basePx.x + int(o.x), 0, 2047), clamp(basePx.y + int(o.y), y0, y0 + 2047));
        const float mapDepth = gShadow0.Load(int3(sp, 0));
        litSum += (receiver + bias) >= mapDepth ? 1.0 : 0.0;
    }
    return litSum / 12.0;
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
