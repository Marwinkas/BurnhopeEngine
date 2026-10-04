#include "frame.slang"
#include "camera.slang"

struct LightGpu {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

struct DecalGpu {
    float4 centerRadius;
    float4 axisXOpacity;
    float4 axisYTex;
    float4 axisZPad;
    float4 color;
};

[[vk::binding(25, 0)]] StructuredBuffer<LightGpu> gLights;
[[vk::binding(18, 0)]] StructuredBuffer<DecalGpu> gDecals;
[[vk::binding(26, 0)]] RWStructuredBuffer<uint4> gCells;
[[vk::binding(27, 0)]] RWStructuredBuffer<uint> gIds;

[shader("compute")]
[numthreads(64, 1, 1)]
void clusterCs(uint3 id : SV_DispatchThreadID) {
    const uint cell = id.x;
    if (cell >= 16u * 9u * 24u) {
        return;
    }
    const uint x = cell % 16u;
    const uint y = (cell / 16u) % 9u;
    const uint z = cell / 144u;
    const float nearZ = max(gFrame.cameraPos.w, 0.05);
    const float far = max(gFrame.sunSplit.z, nearZ + 1.0);
    const float z0 = nearZ * pow(far / nearZ, float(z) / 24.0);
    const float z1 = nearZ * pow(far / nearZ, float(z + 1u) / 24.0);
    const uint lightCount = min(uint(gFrame.padInv.y), 10240u);
    const uint decalCount = gFrame.debugPad0;
    const uint base = cell * 72u;
    const float x0 = float(x) / 16.0;
    const float x1 = float(x + 1u) / 16.0;
    const float y0 = float(y) / 9.0;
    const float y1 = float(y + 1u) / 9.0;
    uint n = 0;
    for (uint i = 0; i < lightCount && n < 64u; ++i) {
        const LightGpu lamp = gLights[i];
        if (lamp.meta.x == 0u) {
            continue;
        }
        const float radius = max(lamp.posRadius.w, 0.1);
        const float4 clip = worldToClip(lamp.posRadius.xyz);
        const float viewZ = clip.w;
        if (viewZ + radius < z0 || viewZ - radius > z1) {
            continue;
        }
        if (viewZ > radius * 0.5) {
            const float2 ndc = clip.xy / max(clip.w, 1e-3) * 0.5 + 0.5;
            const float pad = radius / max(viewZ, 0.2);
            if (ndc.x + pad < x0 || ndc.x - pad > x1 || ndc.y + pad < y0 || ndc.y - pad > y1) {
                continue;
            }
        }
        gIds[base + n] = i;
        n += 1;
    }
    const uint decalBase = base + 64u;
    uint dn = 0u;
    for (uint d = 0u; d < decalCount && dn < 8u; ++d) {
        const DecalGpu decal = gDecals[d];
        const float radius = max(decal.centerRadius.w, 0.05);
        const float4 clip = worldToClip(decal.centerRadius.xyz);
        const float viewZ = clip.w;
        if (viewZ + radius < z0 || viewZ - radius > z1 || viewZ < 1e-3) {
            continue;
        }
        const float2 ndc = clip.xy / clip.w * 0.5 + 0.5;
        const float pad = radius / max(viewZ, 0.2);
        if (ndc.x + pad < x0 || ndc.x - pad > x1 || ndc.y + pad < y0 || ndc.y - pad > y1) {
            continue;
        }
        gIds[decalBase + dn] = d;
        dn += 1u;
    }
    gCells[cell] = uint4(base, n, decalBase, dn);
}
