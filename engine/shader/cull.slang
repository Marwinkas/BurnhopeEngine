#include "frame.slang"
#include "core/hiz_atlas.slang"

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
    return atlasMipY(mip, gFrame.extent.y);
}

struct FlightPush {
    uint flight;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
FlightPush gPush;

float hizLoad(uint2 p, uint mip) {
    const int3 at = int3(int(p.x), int(mipY(mip) + p.y), 0);
    return gPush.flight == 0u ? gHiZ0.Load(at) : gHiZ1.Load(at);
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
float hizBoxMin(float2 uvMin, float2 uvMax, uint mip) {
    const uint2 ext = max(gFrame.extent.xy >> mip, uint2(1, 1));
    const uint2 p0 = uint2(clamp(uvMin, 0.0, 0.9999) * float2(ext));
    const uint2 p1 = uint2(clamp(uvMax, 0.0, 0.9999) * float2(ext));
    const float a = hizLoad(p0, mip);
    const float b = hizLoad(uint2(p1.x, p0.y), mip);
    const float c = hizLoad(uint2(p0.x, p1.y), mip);
    const float d = hizLoad(p1, mip);
    return min(min(a, b), min(c, d));
}

bool coneHidden(GpuInstance inst) {
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    if (meshlet.cone.w <= -0.99) {
        return false;
    }
    // Перевод оси конуса из локального пространства мешлета в мировое пространство инстанса
    const float3 localAxis = meshlet.cone.xyz;
    float3 worldAxis = float3(
        dot(inst.world0.xyz, localAxis),
        dot(inst.world1.xyz, localAxis),
        dot(inst.world2.xyz, localAxis)
    );
    const float axisLen2 = dot(worldAxis, worldAxis);
    if (axisLen2 < 1e-12) {
        return false;
    }
    worldAxis *= rsqrt(axisLen2);

    const float3 toEye = gFrame.cameraPos.xyz - inst.sphere.xyz;
    const float dist = length(toEye);
    const float r = inst.sphere.w;
    if (dist <= r + 1e-4) {
        return false;
    }
    const float c = saturate(meshlet.cone.w);
    const float s = sqrt(max(0.0, 1.0 - c * c));
    // Консервативный учет радиуса сферы мешлета r:
    return dot(toEye, worldAxis) < -s * dist - r;
}
float3 groupSphereWorld(GpuInstance inst, MeshletRef meshlet, out float radiusWorld) {
    if (meshlet.groupSphere.w > 1e-4) {
        const float4 h = float4(meshlet.groupSphere.xyz, 1.0);
        const float sx = length(inst.world0.xyz);
        const float sy = length(inst.world1.xyz);
        const float sz = length(inst.world2.xyz);
        radiusWorld = meshlet.groupSphere.w * max(sx, max(sy, sz));
        return float3(dot(inst.world0, h), dot(inst.world1, h), dot(inst.world2, h));
    }
    radiusWorld = inst.sphere.w;
    return inst.sphere.xyz;
}
float4x4 cullView() {
    if ((gPass.passMask & 64u) != 0u) {
        return gFrame.freezeVp;
    }
    return gFrame.viewProj;
}

float screenRadiusPx(float3 center, float radius, float4 clip) {
    const float z = max(abs(clip.w), 0.1);
    const float4 up = mul(cullView(), float4(center + float3(0.0, radius, 0.0), 1.0));
    const float2 d = up.xy / max(abs(up.w), 0.1) - clip.xy / z;
    return length(d) * float(gFrame.extent.y) * 0.5;
}


bool lodOff(GpuInstance inst, float4 clip) {
    const uint lod = inst.pad.x;
    if (gPass.forceLod >= 0) {
        return lod != uint(gPass.forceLod);
    }
    const bool chain = (inst.pad.y & 0x80000000u) != 0u;
    const uint maxLod = (inst.pad.y >> 28) & 7u;
    if (!chain || gFrame.disableLod != 0 || maxLod == 0u) {
        return lod != 0u;
    }
    if (lod > maxLod) {
        return true;
    }

    const MeshletRef meshlet = gMeshlets[inst.meshId];
    float radius = inst.sphere.w;
    const float3 center = groupSphereWorld(inst, meshlet, radius);
    const float4 groupClip = mul(cullView(), float4(center, 1.0));
    if (groupClip.w <= 0.1) {
        return lod != 0u;
    }

    const float px = screenRadiusPx(center, radius, groupClip);

    const float t0 = 24.0;
    const float t1 = 12.0;
    const float t2 = 6.0;

    if (lod == 0u) {
        return px < t0;
    }
    if (lod == 1u) {
        return (px >= t0) || (maxLod > 1u && px < t1);
    }
    if (lod == 2u) {
        return (px >= t1) || (maxLod > 2u && px < t2);
    }
    return px >= t2;
}


static const uint kDbgStride = 16u;
static const uint kDbgBase = 64u;

uint dbgAt() {
    return kDbgBase + gPush.flight * kDbgStride;
}

// 0 keep, 1 lod, 2 behind, 3 frustum, 4 cone, 5 hiz.
uint cullCode(GpuInstance inst, bool late) {
    const float r = inst.sphere.w;
    const float4 clip = mul(cullView(), float4(inst.sphere.xyz, 1.0));
    if (lodOff(inst, clip)) {
        return 1u;
    }
    const float depth = clip.w;
    const float nearZ = max(gFrame.cameraPos.w, 0.05);
    if (depth - r <= nearZ) {
        if (depth + r < 0.0) {
            if (gFrame.disableBehind == 0) {
                return 2u;
            }
        } else {
            InterlockedAdd(gArgs[dbgAt() + 6u], 1u);
            return 0u;
        }
    }
    if (gFrame.disableFrustum == 0 && outsideFrustum(clip, r)) {
        return 3u;
    }
    if (gFrame.disableConeCull == 0 && coneHidden(inst)) {
        return 4u;
    }
    const float radPx = screenRadiusPx(inst.sphere.xyz, r, clip);
    if (gFrame.disableSubpixel == 0 && radPx < 0.75) {
        return 8u;
    }
    if (!late || gFrame.disableHiZ != 0) {
        return 0u;
    }

    const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
    const float2 uvRadius = float2(radPx * gFrame.invExtent.x, radPx * gFrame.invExtent.y);
    const float2 uvMin = uv - uvRadius;
    const float2 uvMax = uv + uvRadius;
    if (uvMax.x < 0.0 || uvMax.y < 0.0 || uvMin.x > 1.0 || uvMin.y > 1.0) {
        return 0u;
    }

    const float3 toCenter = inst.sphere.xyz - gFrame.cameraPos.xyz;
    const float dist = length(toCenter);
    const float3 nearest = dist > 1e-3 ? inst.sphere.xyz - toCenter * (r / dist) : inst.sphere.xyz;
    const float4 nc = mul(cullView(), float4(nearest, 1.0));
    if (nc.w <= nearZ) {
        return 0u;
    }
    const float zNear = saturate(nc.z / nc.w);

    const float diamPx = max(radPx * 2.0, 1.0);
    if (diamPx > 64.0) {
        return 0u;
    }
    const uint mip = min(uint(ceil(log2(diamPx))), 5u);
    const float minOccDepth = hizBoxMin(uvMin, uvMax, mip);
    return zNear < minOccDepth - 1e-4 ? 5u : 0u;
}
void emitOne(uint3 id, bool late) {
    const uint pack = gPush.flight * 16u;
    const uint count = gArgs[pack + 4u];
    const uint visBase = gArgs[pack + 6u];
    const uint flagRead = gArgs[pack + 7u];
    const uint flagWrite = gArgs[pack + 12u];
    if (id.x >= count) {
        return;
    }

    const bool hizOn = (gFrame.disableHiZ == 0);
    const uint prevFlag = hizOn ? gFlags[flagRead + id.x] : 1u;

    if (!late) {
        if (prevFlag == 2u) {
            InterlockedAdd(gArgs[dbgAt()], 1u);
            return;
        }
    } else if (prevFlag != 2u) {
        if (gFlags[flagWrite + id.x] == 3u) {
            const GpuInstance drawn = gInstances[id.x];
            const uint again = cullCode(drawn, true);
            gFlags[flagWrite + id.x] = (hizOn && again == 5u) ? 2u : 1u;
        }
        return;
    }

    const GpuInstance inst = gInstances[id.x];
    const uint sector = gSector[id.x];
    if (gFrame.disableFrustum == 0 && sector < 32u && (gFrame.debugPad2 & (1u << sector)) == 0u) {
        InterlockedAdd(gArgs[dbgAt() + 3u], 1u);
        gFlags[flagWrite + id.x] = 0u;
        return;
    }
    const uint code = cullCode(inst, late);
    if (code != 0u) {
        InterlockedAdd(gArgs[dbgAt() + code], 1u);
        gFlags[flagWrite + id.x] = (hizOn && code == 5u) ? 2u : 0u;
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
    const uint pack = gPush.flight * 16u;
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
