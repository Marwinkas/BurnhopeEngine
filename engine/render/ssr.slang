// Screen-space reflection on visbuffer depth. Same frame path as shade, not G-Buffer.

struct FrameGPU {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 sunDirIntensity;
    float4 sunColor;
    float4 cameraPos;
    float2 invExtent;
    float2 padInv;
    uint2 extent;
    uint2 padExt;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameGPU> gFrame;
[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(4, 0)]] Texture2D<float4> gHdrA;
[[vk::binding(5, 0)]] RWTexture2D<float4> gHdrB;
[[vk::binding(1, 1)]] SamplerState gLinear;

float3 reconstructWorld(float2 uv, float depth) {
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 world = mul(gFrame.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

float3 worldToUv(float3 world, out float depth) {
    float4 clip = mul(gFrame.viewProj, float4(world, 1.0));
    const float3 ndc = clip.xyz / clip.w;
    depth = ndc.z;
    return float3(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5, ndc.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
    const float4 lit = gHdrA.SampleLevel(gLinear, uv, 0);
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0) {
        gHdrB[px] = lit;
        return;
    }

    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(uv, depth);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);

    // Face normal from neighbors in screen (SSR first cut; meshlet bary later).
    const float2 texel = gFrame.invExtent;
    const float3 Px = reconstructWorld(uv + float2(texel.x, 0), gDepth.SampleLevel(gLinear, uv + float2(texel.x, 0), 0).r);
    const float3 Py = reconstructWorld(uv + float2(0, texel.y), gDepth.SampleLevel(gLinear, uv + float2(0, texel.y), 0).r);
    float3 N = normalize(cross(Px - P, Py - P));
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    const float3 R = reflect(-V, N);
    float3 hit = 0.0;
    bool found = false;
    float3 march = P;
    const float step = 0.08;
    [unroll]
    for (int i = 0; i < 24; ++i) {
        march += R * step;
        float rayZ;
        const float3 suv = worldToUv(march, rayZ);
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) {
            break;
        }
        const float sceneZ = gDepth.SampleLevel(gLinear, suv.xy, 0).r;
        if (rayZ > sceneZ + 0.002 && rayZ < sceneZ + 0.08) {
            hit = gHdrA.SampleLevel(gLinear, suv.xy, 0).rgb;
            found = true;
            break;
        }
    }

    const float F = 0.08 + 0.92 * pow(1.0 - saturate(dot(N, V)), 5.0);
    const float3 color = lit.rgb + (found ? hit * F * 0.55 : 0.0);
    gHdrB[px] = float4(color, 1.0);
}
