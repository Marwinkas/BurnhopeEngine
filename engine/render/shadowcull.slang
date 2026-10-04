#include "frame.slang"

// Cascade cull. The ortho matrix already covers casters whose shadow
// reaches the camera slice, including meshes outside the view frustum.

[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(18, 0)]] RWStructuredBuffer<uint> gShadow;
[[vk::binding(32, 0)]] RWStructuredBuffer<uint> gArgs;

bool cascadeOut(float4x4 vp, float3 center, float radius, float extent) {
    const float4 clip = mul(vp, float4(center, 1.0));
    const float w = max(abs(clip.w), 1e-3);
    const float2 ndc = clip.xy / w;
    const float z = clip.z / w;
    const float pad = radius / max(extent, 1.0);
    return ndc.x < -1.0 - pad || ndc.x > 1.0 + pad
        || ndc.y < -1.0 - pad || ndc.y > 1.0 + pad
        || z < -pad || z > 1.0 + pad;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void shadowClear(uint3 id : SV_DispatchThreadID) {
    const uint base = uint(gFrame.sunSplit.w);
    [unroll]
    for (uint c = 0; c < 3; ++c) {
        gArgs[base + c * 4u + 0u] = 0u;
        gArgs[base + c * 4u + 1u] = 1u;
        gArgs[base + c * 4u + 2u] = 1u;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void shadowCull(uint3 id : SV_DispatchThreadID) {
    const uint base = uint(gFrame.sunSplit.w);
    const uint count = gArgs[base + 12u];
    if (id.x >= count) {
        return;
    }
    const GpuInstance inst = gInstances[id.x];
    const uint lod = inst.pad.x;
    const bool chain = (inst.pad.y & 0x80000000u) != 0u;
    const uint maxLod = (inst.pad.y >> 28) & 7u;
    const float stride = gFrame.padInv.x;
    const float radius = inst.sphere.w;
    [unroll]
    for (uint c = 0; c < 3; ++c) {
        if (chain && c == 0u && lod > 1u) {
            continue;
        }
        if (chain && c == 1u && lod > 2u) {
            continue;
        }
        if (chain && c == 0u && lod == 0u && maxLod > 0u && radius < 0.2) {
            continue;
        }
        if (chain && c >= 1u && lod == 0u && maxLod > 0u && radius < 1.5) {
            continue;
        }
        if (chain && c == 2u) {
            const uint farLod = maxLod == 0u ? 0u : min(maxLod, 2u);
            if (lod != farLod) {
                continue;
            }
        }
        const float extent = max(gFrame.sunSplit[c], 1.0);
        const float pixels = radius / extent * 2048.0;
        const float minPx = c == 0u ? 8.0 : c == 1u ? 4.0 : 3.0;
        if (pixels < minPx) {
            continue;
        }
        if (cascadeOut(gFrame.sunCascade[c], inst.sphere.xyz, radius, extent)) {
            continue;
        }
        uint slot;
        InterlockedAdd(gArgs[base + c * 4u], 1u, slot);
        gShadow[uint(stride) * c + slot] = id.x;
    }
}
