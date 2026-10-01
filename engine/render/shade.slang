// Compute shade: visbuffer + depth + meshlet SSBO → HDR PBR (1 directional).

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

struct Vertex {
    float3 pos;
    float pad0;
    float3 nrm;
    float pad1;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameGPU> gFrame;
[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;

static const float kPi = 3.14159265;

float D_GGX(float NdotH, float a) {
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float G_Smith(float NdotV, float NdotL, float a) {
    const float r = a + 1.0;
    const float k = (r * r) / 8.0;
    const float gv = NdotV / (NdotV * (1.0 - k) + k);
    const float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 f0) {
    const float t = pow(1.0 - VdotH, 5.0);
    return f0 + (1.0 - f0) * t;
}

float3 reconstructWorld(uint2 px, float depth) {
    const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 world = mul(gFrame.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void shadeCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0) {
        gHdr[px] = float4(0.07, 0.09, 0.12, 1.0);
        return;
    }

    const uint tri = vis & 255;
    const uint i0 = gIdx[tri * 3 + 0];
    const uint i1 = gIdx[tri * 3 + 1];
    const uint i2 = gIdx[tri * 3 + 2];
    const float3 p0 = gVerts[i0].pos;
    const float3 p1 = gVerts[i1].pos;
    const float3 p2 = gVerts[i2].pos;
    float3 N = normalize(cross(p1 - p0, p2 - p0));

    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(px, depth);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }

    const float3 L = normalize(gFrame.sunDirIntensity.xyz);
    const float3 H = normalize(V + L);
    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    const float3 albedo = float3(0.78, 0.34, 0.16);
    const float metallic = 0.0;
    const float roughness = 0.42;
    const float a = max(roughness * roughness, 0.002);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 F = F_Schlick(VdotH, f0);
    const float D = D_GGX(NdotH, a);
    const float G = G_Smith(NdotV, NdotL, a);
    const float3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    const float3 diff = albedo * (1.0 / kPi) * (1.0 - F) * (1.0 - metallic);
    const float3 radiance = gFrame.sunColor.xyz * gFrame.sunDirIntensity.w;
    const float3 color = (diff + spec) * NdotL * radiance + albedo * 0.04;
    gHdr[px] = float4(color, 1.0);
}
