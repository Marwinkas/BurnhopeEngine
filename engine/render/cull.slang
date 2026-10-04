#include "frame.slang"

// Two-phase occlusion. Phase early draws what was visible last frame.
// The depth of that draw builds the Hi-Z pyramid. Phase late tests the rest
// against that pyramid. The mesh shader does not sample Hi-Z.

[[vk::binding(6, 0)]] Texture2D<float> gHiZ0;
[[vk::binding(31, 0)]] Texture2D<float> gHiZ1;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gSector;
[[vk::binding(11, 0)]] RWStructuredBuffer<uint> gVisible;
[[vk::binding(28, 0)]] RWStructuredBuffer<uint> gFlags;
[[vk::binding(29, 0)]] RWStructuredBuffer<uint> gArgs;

uint mipY(uint mip) {
    uint y = 0;
    uint h = gFrame.extent.y;
    for (uint i = 0; i < mip; ++i) {
        y += h;
        h = max(h >> 1, 1u);
    }
    return y;
}

#ifndef CULL_FLIGHT
#define CULL_FLIGHT 0
#endif

float hizLoad(uint2 p, uint mip) {
    const int3 at = int3(int(p.x), int(mipY(mip) + p.y), 0);
#if CULL_FLIGHT == 0
    return gHiZ0.Load(at);
#else
    return gHiZ1.Load(at);
#endif
}

float hizAt(float2 uv, uint mip) {
    const uint2 ext = max(gFrame.extent.xy >> mip, uint2(1, 1));
    const uint2 i = uint2(clamp(uv, 0.0, 0.999) * float2(ext));
    const uint2 j = min(i + 1, ext - 1);
    const float a = hizLoad(i, mip);
    const float b = hizLoad(uint2(j.x, i.y), mip);
    const float c = hizLoad(uint2(i.x, j.y), mip);
    const float d = hizLoad(j, mip);
    return min(min(a, b), min(c, d));
}

bool outsideFrustum(float4 clip, float radius) {
    if (clip.w <= 1e-3) {
        return true;
    }
    const float pad = radius / clip.w;
    const float2 ndc = clip.xy / clip.w;
    const float z = clip.z / clip.w;
    return ndc.x < -1.0 - pad || ndc.x > 1.0 + pad || ndc.y < -1.0 - pad || ndc.y > 1.0 + pad || z < -pad || z > 1.0 + pad;
}

bool coneHidden(GpuInstance inst) {
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    if (meshlet.cone.w <= -0.99) {
        return false;
    }
    const float3 toEye = gFrame.cameraPos.xyz - inst.sphere.xyz;
    const float len2 = dot(toEye, toEye);
    if (len2 < 1e-8) {
        return false;
    }
    const float facing = dot(toEye / sqrt(len2), meshlet.cone.xyz);
    const float c = saturate(meshlet.cone.w);
    const float s = sqrt(max(0.0, 1.0 - c * c));
    return facing < -s;
}

float4x4 cullView() {
    if ((gPass.passMask & 64u) != 0u) {
        return gFrame.freezeVp;
    }
    return gFrame.viewProj;
}

float screenRadiusPx(GpuInstance inst, float4 clip) {
    const float z = max(abs(clip.w), 0.1);
    const float4 up = mul(cullView(), float4(inst.sphere.xyz + float3(0.0, inst.sphere.w, 0.0), 1.0));
    const float2 d = up.xy / max(abs(up.w), 0.1) - clip.xy / z;
    return length(d) * float(gFrame.extent.y) * 0.5;
}

bool lodOff(GpuInstance inst, float4 clip) {
    const uint lod = inst.pad.x;
    if (gPass.forceLod >= 0) {
        return lod != uint(gPass.forceLod);
    }
    const bool chain = (inst.pad.y & 0x80000000u) != 0u;
    uint maxLod = (inst.pad.y >> 28) & 7u;
    if (!chain) {
        return lod != 0u;
    }
    if (gFrame.disableLod != 0) {
        return lod != 0u;
    }
    if (lod > maxLod) {
        return true;
    }
    const float j = (float(inst.meshId & 15u) / 15.0 - 0.5) * 0.3;
    const float enter = 36.0 * (1.0 + j);
    const float leave = 24.0 * (1.0 + j);
    const float px = screenRadiusPx(inst, clip);
    if (lod == 0u) {
        if (maxLod == 0u) {
            return false;
        }
        return px < leave;
    }
    if (lod == 1u) {
        if (maxLod <= 1u) {
            return px >= enter;
        }
        return px >= enter || px < 8.0 * (1.0 + j);
    }
    if (lod == maxLod) {
        const float top = lod == 2u ? 22.0 : 14.0;
        return px >= top * (1.0 + j);
    }
    const float top = 22.0;
    const float bot = 5.0;
    return px >= top * (1.0 + j) || px < bot;
}

static const uint kDbgStride = 16u;
static const uint kDbgBase = 64u;

uint dbgAt() {
    return kDbgBase + CULL_FLIGHT * kDbgStride;
}

// 0 keep, 1 lod, 2 behind, 3 frustum, 4 cone, 5 hiz.
uint cullCode(GpuInstance inst, bool late) {
    const float r = inst.sphere.w;
    const float4 clip = mul(cullView(), float4(inst.sphere.xyz, 1.0));
    if (lodOff(inst, clip)) {
        return 1u;
    }
    const float depth = clip.w;
    const float nearZ = gFrame.cameraPos.w;
    if (depth - r <= nearZ) {
        if (depth + r < 0.0) {
            return 2u;
        }
        InterlockedAdd(gArgs[dbgAt() + 6u], 1u);
        return 0u;
    }
    if (gFrame.disableFrustum == 0 && outsideFrustum(clip, r)) {
        return 3u;
    }
    if (gFrame.disableConeCull == 0 && coneHidden(inst)) {
        return 4u;
    }
    if (gFrame.disableSubpixel == 0 && screenRadiusPx(inst, clip) < 1.5) {
        return 8u;
    }
    if (!late) {
        return 0u;
    }
    const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
        return 0u;
    }
    const float3 toCenter = inst.sphere.xyz - gFrame.cameraPos.xyz;
    const float dist = length(toCenter);
    const float3 nearest = dist > 1e-3 ? inst.sphere.xyz - toCenter * (r / dist) : inst.sphere.xyz;
    const float4 nc = mul(cullView(), float4(nearest, 1.0));
    const float zNear = nc.w > 1e-4 ? saturate(nc.z / nc.w) : 0.0;
    const float pixels = max(r / clip.w * float(gFrame.extent.y) * 2.0, 1.0);
    if (gFrame.disableHiZ != 0) {
        return 0u;
    }
    const uint mip = min(uint(floor(log2(pixels))), 5u);
    return zNear < hizAt(uv, mip) - 0.0015 ? 5u : 0u;
}

void emitOne(uint3 id, bool late) {
    const uint pack = CULL_FLIGHT * 16u;
    const uint count = gArgs[pack + 4u];
    const uint visBase = gArgs[pack + 6u];
    const uint flagRead = gArgs[pack + 7u];
    const uint flagWrite = gArgs[pack + 12u];
    if (id.x >= count) {
        return;
    }
    // 0 outside the frustum, 1 drawn and confirmed, 2 occluded by Hi-Z,
    // 3 drawn this frame and waiting for the pyramid built from that draw.
    // A turn sends flag 0 through the early draw. Stale Hi-Z only tests flag 2.
    const uint prevFlag = gFlags[flagRead + id.x];
    if (!late) {
        if (prevFlag == 2u) {
            InterlockedAdd(gArgs[dbgAt()], 1u);
            return;
        }
    } else if (prevFlag != 2u) {
        if (gFlags[flagWrite + id.x] == 3u) {
            const GpuInstance drawn = gInstances[id.x];
            const uint again = cullCode(drawn, true);
            gFlags[flagWrite + id.x] = again == 5u ? 2u : 1u;
        }
        return;
    }
    const GpuInstance inst = gInstances[id.x];
    const uint sector = gSector[id.x];
    if (sector < 32u && (gFrame.debugPad2 & (1u << sector)) == 0u) {
        InterlockedAdd(gArgs[dbgAt() + 3u], 1u);
        gFlags[flagWrite + id.x] = 0u;
        return;
    }
    const uint code = cullCode(inst, late);
    if (code != 0u) {
        InterlockedAdd(gArgs[dbgAt() + code], 1u);
        gFlags[flagWrite + id.x] = code == 5u ? 2u : 0u;
        return;
    }
    uint slot;
    InterlockedAdd(gArgs[pack + (late ? 8u : 0u)], 1u, slot);
    gVisible[visBase + (late ? uint(gFrame.padInv.x) : 0u) + slot] = id.x;
    gFlags[flagWrite + id.x] = late ? 1u : 3u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void cullClear(uint3 id : SV_DispatchThreadID) {
    const uint pack = CULL_FLIGHT * 16u;
    gArgs[pack + 0u] = 0u;
    gArgs[pack + 1u] = 1u;
    gArgs[pack + 2u] = 1u;
    gArgs[pack + 8u] = 0u;
    gArgs[pack + 9u] = 1u;
    gArgs[pack + 10u] = 1u;
    const uint dbg = dbgAt();
    [unroll]
    for (uint i = 0u; i < 9u; ++i) {
        gArgs[dbg + i] = 0u;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void meshletCull(uint3 id : SV_DispatchThreadID) {
    emitOne(id, false);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void meshletCullLate(uint3 id : SV_DispatchThreadID) {
    emitOne(id, true);
}
