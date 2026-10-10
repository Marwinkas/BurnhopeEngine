// Карты солнца. Ожидает gShadow0 и gFrame. Лампы и туман этот файл не считают.
// Глубина reverse-Z: ближе к солнцу значение больше. Пустой тексель 0 — свет.

float shadowTexelLit(int2 sp, int y0, float receiver, float bias) {
    sp.x = clamp(sp.x, 0, 2047);
    sp.y = clamp(sp.y, y0, y0 + 2047);
    const float mapDepth = gShadow0.Load(int3(sp, 0));
    return (receiver + bias) >= mapDepth ? 1.0 : 0.0;
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
    const float extent = max(gSun.sunSplit[cascade], 1.0);
    const float bias = 0.05 / (extent * 8.0);
    const int y0 = int(cascade) * 2048;
    const float2 coord = saturate(sunLocal) * 2048.0 - 0.5;
    const int2 ip = int2(floor(coord));
    const float2 f = coord - float2(ip);
    const float s00 = shadowTexelLit(int2(ip.x, ip.y + y0), y0, receiver, bias);
    const float s10 = shadowTexelLit(int2(ip.x + 1, ip.y + y0), y0, receiver, bias);
    const float s01 = shadowTexelLit(int2(ip.x, ip.y + 1 + y0), y0, receiver, bias);
    const float s11 = shadowTexelLit(int2(ip.x + 1, ip.y + 1 + y0), y0, receiver, bias);
    return lerp(lerp(s00, s10, f.x), lerp(s01, s11, f.x), f.y);
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
    const float bias = 0.05 / (max(gSun.sunSplit[cascade], 1.0) * 8.0);
    const int y0 = int(cascade) * 2048;
    const int2 sp = int2(sunLocal * 2048.0) + int2(0, y0);
    return shadowTexelLit(sp, y0, receiver, bias);
}
