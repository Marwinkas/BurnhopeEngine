// HDR → swapchain. AgX, two-scale Karis bloom, grain, vignette, chromatic aberration.
// Strengths live in FrameGPU.post.

#include "frame.slang"

[[vk::binding(6, 0)]] Texture2D<float4> gHdr;
[[vk::binding(33, 0)]] Texture2D<float4> gBloom;
[[vk::binding(1, 1)]] SamplerState gLinear;

struct VertOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void tonemapMesh(
    uint gtid : SV_GroupThreadID,
    out vertices VertOut verts[3],
    out indices uint3 prims[1])
{
    if (gtid == 0) {
        SetMeshOutputCounts(3, 1);
        prims[0] = uint3(0, 1, 2);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < 3) {
        const float2 ndc = float2((gtid << 1) & 2, gtid & 2) * 2.0 - 1.0;
        verts[gtid].pos = float4(ndc, 0.0, 1.0);
        verts[gtid].uv = float2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5);
    }
}

float3 agxContrast(float3 x) {
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 agx(float3 val) {
    const float3x3 inset = float3x3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const float3x3 outset = float3x3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    val = mul(inset, max(val, 0.0));
    val = clamp(log2(max(val, 1e-10)), -12.47393, 4.026069);
    val = (val - (-12.47393)) / (4.026069 - (-12.47393));
    val = saturate(agxContrast(val));
    return max(mul(outset, val), 0.0);
}

[shader("fragment")]
float4 tonemapFs(VertOut vin) : SV_Target0 {
    const float2 d = vin.uv - 0.5;
    const float ca = gTone.ca;
    const float2 shift = d * ca * gFrame.invExtent;
    float3 hdr;
    hdr.r = gHdr.SampleLevel(gLinear, vin.uv + shift, 0.0).r;
    hdr.g = gHdr.SampleLevel(gLinear, vin.uv, 0.0).g;
    hdr.b = gHdr.SampleLevel(gLinear, vin.uv - shift, 0.0).b;
    if (gTone.bloomMix > 0.001 && (gPass.passMask & 32u) != 0u) {
        const uint h0 = max(gFrame.extent.y >> 1, 1u);
        const uint h1 = max(gFrame.extent.y >> 2, 1u);
        const uint h2 = max(gFrame.extent.y >> 3, 1u);
        const uint h3 = max(gFrame.extent.y >> 4, 1u);
        const float span = float(h0 + h1 + h2 + h3);
        const float2 uvb = float2(vin.uv.x, vin.uv.y * (float(h0) / span));
        hdr += gBloom.SampleLevel(gLinear, uvb, 0.0).rgb * gTone.bloomMix;
    }
    float3 mapped;
    if (gTone.tonemapper == 1u) {
        mapped = saturate((hdr * (2.51 * hdr + 0.03)) / (hdr * (2.43 * hdr + 0.59) + 0.14));
    } else if (gTone.tonemapper == 2u) {
        mapped = hdr / (1.0 + hdr);
    } else if (gTone.tonemapper == 3u) {
        mapped = max(hdr, 0.0);
    } else {
        mapped = agx(hdr);
    }
    float3 color = mapped;
    const float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    if (gTone.grain > 0.001) {
        const float n = frac(sin(dot(vin.uv * float2(gFrame.extent.xy) + gFrame.cameraPos.xy, float2(12.9898, 78.233))) * 43758.5453);
        color += (n - 0.5) * gTone.grain * saturate(1.15 - luma);
    }
    color *= saturate(1.15 - dot(d, d) * gTone.vignette);
    return float4(saturate(color), 1.0);
}
