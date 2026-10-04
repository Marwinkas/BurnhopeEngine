// Один раз после цикла кубмапы. Та же оценка, что probeEval: константа серого даёт серый.

#include "core/math.slang"

struct ProbeGpu {
    float4 band0;
    float4 band1;
    float4 band2;
    float4 band3;
    float4 band4;
    float4 band5;
    float4 band6;
    float4 band7;
};

[[vk::binding(16, 0)]] Texture2D<float4> gCube;
[[vk::binding(19, 0)]] RWStructuredBuffer<ProbeGpu> gProbes;
[[vk::binding(24, 0)]] SamplerState gWrap;

float3 sampleAtlas(float3 dir) {
    uint face = 0u;
    float2 uv = 0.0;
    cubeDirToFaceUV(dir, face, uv);
    return gCube.SampleLevel(gWrap, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

float shY(uint band, float3 d) {
    const float b0 = 0.2820948;
    const float b1 = 0.4886025;
    const float b2 = 1.0925484;
    const float b3 = 0.3153916;
    const float b4 = 0.5462742;
    if (band == 0u) return b0;
    if (band == 1u) return b1 * d.y;
    if (band == 2u) return b1 * d.z;
    if (band == 3u) return b1 * d.x;
    if (band == 4u) return b2 * d.x * d.y;
    if (band == 5u) return b2 * d.y * d.z;
    if (band == 6u) return b3 * (3.0 * d.z * d.z - 1.0);
    if (band == 7u) return b2 * d.x * d.z;
    return b4 * (d.x * d.x - d.y * d.y);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void probeBake(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= 64u) {
        return;
    }
    float3 acc[9] = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const float weight = 12.5663706 / 32.0;
    [loop]
    for (uint i = 0u; i < 32u; ++i) {
        const float y = 1.0 - (float(i) + 0.5) * (2.0 / 32.0);
        const float r = sqrt(max(1.0 - y * y, 0.0));
        const float phi = float(i) * 2.3999632;
        const float3 dir = float3(cos(phi) * r, y, sin(phi) * r);
        const float3 color = sampleAtlas(dir);
        [unroll]
        for (uint band = 0u; band < 9u; ++band) {
            acc[band] += color * shY(band, dir) * weight;
        }
    }
    ProbeGpu pr;
    pr.band0 = float4(acc[0].x, acc[0].y, acc[0].z, acc[1].x);
    pr.band1 = float4(acc[1].y, acc[1].z, acc[2].x, acc[2].y);
    pr.band2 = float4(acc[2].z, acc[3].x, acc[3].y, acc[3].z);
    pr.band3 = float4(acc[4].x, acc[4].y, acc[4].z, acc[5].x);
    pr.band4 = float4(acc[5].y, acc[5].z, acc[6].x, acc[6].y);
    pr.band5 = float4(acc[6].z, acc[7].x, acc[7].y, acc[7].z);
    pr.band6 = float4(acc[8].x, acc[8].y, acc[8].z, 0.0);
    pr.band7 = 0.0;
    gProbes[dtid.x] = pr;
}
