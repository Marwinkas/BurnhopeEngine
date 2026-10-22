// Карта тени облаков: 1024² сверху, мир 8192 м, тексель 8 м, привязка к сетке камеры.

#include "frame.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"
#include "cloud_density.slang"

[[vk::binding(44, 0)]] RWTexture2D<float> gMap;
[[vk::binding(46, 0)]] StructuredBuffer<float> gCloudNoise;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

[shader("compute")]
[numthreads(8, 8, 1)]
void cloudShadowCs(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 1024u || id.y >= 1024u) {
        return;
    }
    if (gAtm.cloudCoverage <= 0.01 || gAtm.cloudDensity <= 0.0005) {
        gMap[id.xy] = 1.0;
        return;
    }
    const float cell = 8.0;
    const float2 origin = floor(gFrame.cameraPos.xz / cell) * cell;
    const float2 uv = (float2(id.xy) + 0.5) / 1024.0;
    const float2 xz = origin + (uv - 0.5) * 8192.0;
    const float bottom = max(gAtm.cloudBottom, 1200.0);
    const float top = max(gAtm.cloudTop, bottom + 2200.0);
    const float span = top - bottom;
    const float dt = span / 4.0;
    const float2 windOff = float2(gAtm.cloudSpeedX, gAtm.cloudSpeedZ) * gAtm.time;
    float optical = 0.0;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        const float u = (float(i) + 0.5) / 4.0;
        const float y = bottom + span * u;
        float hFrac = 0.0;
        optical += cloudDensityAt(gCloudNoise, float3(xz.x, y, xz.y), bottom, top, gAtm.cloudCoverage, 0.0, 1.0, windOff, hFrac) * dt * 0.012;
    }
    gMap[id.xy] = exp(-optical);
}
