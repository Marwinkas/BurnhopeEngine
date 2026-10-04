// Пустые тексели грани (альфа 0) берут панораму и короткий марш облака. Геометрия с альфой 1 остаётся.

#include "core/math.slang"
#include "core/atmosphere.slang"

[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(39, 0)]] RWTexture2D<float4> gCube;
[[vk::binding(42, 0)]] Texture2D<float4> gSky;
[[vk::binding(46, 0)]] StructuredBuffer<float> gNoise;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

float noiseTex(float3 uv, uint res, uint base) {
    const float3 q = frac(uv) * float(res);
    const uint3 i = uint3(q) % res;
    return gNoise[base + i.x + i.y * res + i.z * res * res];
}

float3 faceDir(uint face, float2 uv) {
    const float2 n = uv * 2.0 - 1.0;
    if (face == 0u) return normalize(float3(1.0, -n.y, -n.x));
    if (face == 1u) return normalize(float3(-1.0, -n.y, n.x));
    if (face == 2u) return normalize(float3(n.x, 1.0, n.y));
    if (face == 3u) return normalize(float3(n.x, -1.0, -n.y));
    if (face == 4u) return normalize(float3(n.x, -n.y, 1.0));
    return normalize(float3(-n.x, -n.y, -1.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cubeFill(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= 384u || dtid.y >= 64u) {
        return;
    }
    if (gCube[int2(dtid.xy)].a > 0.5) {
        return;
    }
    const uint face = dtid.x / 64u;
    const float2 uv = (float2(dtid.x % 64u, dtid.y) + 0.5) / 64.0;
    const float3 dir = faceDir(face, uv);
    float3 sky = gSky.SampleLevel(gClamp, atmSkyUv(dir), 0.0).rgb;
    if (dir.y > 0.02) {
        const float3 p = dir * gAtm.cloudBottom;
        const float shape = 1.0 - noiseTex(p / 2200.0 / 128.0, 128u, 0u);
        const float detail = noiseTex((p / 380.0 + 19.0) / 32.0, 32u, 128u * 128u * 128u);
        const float dens = saturate(shape - (1.0 - gAtm.cloudCoverage) * 0.55 - detail * gAtm.cloudDetail);
        sky = lerp(sky, float3(0.85, 0.88, 0.92), saturate(dens * 4.0));
    }
    gCube[int2(dtid.xy)] = float4(sky, 0.0);
}
