// HDR → swapchain. ACES-ish filmic, not AgX yet.

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
[[vk::binding(6, 0)]] Texture2D<float4> gHdr;
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

float3 aces(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("fragment")]
float4 tonemapFs(VertOut vin) : SV_Target0 {
    const float3 hdr = gHdr.SampleLevel(gLinear, vin.uv, 0).rgb;
    return float4(pow(aces(hdr), 1.0 / 2.2), 1.0);
}
