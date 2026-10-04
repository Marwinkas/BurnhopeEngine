// Screen-space radiance cascades. Probes sit on the screen. Cascade 0 is dense
// and short; each next cascade has 4x fewer probes, 4x more rays, and the next
// distance interval. A miss keeps alpha = 1 and is merged with the coarser
// cascade: rgb = near + near.a * far, a = near.a * far.a. Rays that leave the
// screen stay empty until world cascades exist.

#include "frame.slang"
#include "camera.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(4, 0)]] Texture2D<float4> gHdr;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(13, 0)]] SamplerState gSamp;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;

static const float kPi = 3.14159265;

float3 faceNormal(uint instanceId, uint tri, float3 P, uint2 px) {
    const GpuInstance inst = gInstances[instanceId];
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint at = meshlet.indexOffset + tri * 3;
    const uint i0 = meshlet.vertexOffset + gIdx[at];
    const uint i1 = meshlet.vertexOffset + gIdx[at + 1];
    const uint i2 = meshlet.vertexOffset + gIdx[at + 2];
    const float4 h0 = float4(gVerts[i0].pos, 1.0);
    const float4 h1 = float4(gVerts[i1].pos, 1.0);
    const float4 h2 = float4(gVerts[i2].pos, 1.0);
    const float3 w0 = float3(dot(inst.world0, h0), dot(inst.world1, h0), dot(inst.world2, h0));
    const float3 w1 = float3(dot(inst.world0, h1), dot(inst.world1, h1), dot(inst.world2, h1));
    const float3 w2 = float3(dot(inst.world0, h2), dot(inst.world1, h2), dot(inst.world2, h2));
    const float3 e1 = w1 - w0;
    const float3 e2 = w2 - w0;
    float3 N = cross(e1, e2);
    N = dot(N, N) > 1e-12 ? normalize(N) : float3(0.0, 1.0, 0.0);
    const float3 n0 = float3(dot(inst.world0.xyz, gVerts[i0].nrm), dot(inst.world1.xyz, gVerts[i0].nrm), dot(inst.world2.xyz, gVerts[i0].nrm));
    const float3 n1 = float3(dot(inst.world0.xyz, gVerts[i1].nrm), dot(inst.world1.xyz, gVerts[i1].nrm), dot(inst.world2.xyz, gVerts[i1].nrm));
    const float3 n2 = float3(dot(inst.world0.xyz, gVerts[i2].nrm), dot(inst.world1.xyz, gVerts[i2].nrm), dot(inst.world2.xyz, gVerts[i2].nrm));
    const float4 c0 = worldToClip(w0);
    const float4 c1 = worldToClip(w1);
    const float4 c2 = worldToClip(w2);
    const float2 s0 = (c0.xy / max(abs(c0.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 s1 = (c1.xy / max(abs(c1.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 s2 = (c2.xy / max(abs(c2.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 sp = float2(px) + 0.5;
    const float2 v0 = s1 - s0;
    const float2 v1 = s2 - s0;
    const float2 v2 = sp - s0;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float denom = d00 * d11 - d01 * d01;
    const float bv = denom > 1e-6 ? (d11 * dot(v2, v0) - d01 * dot(v2, v1)) / denom : 0.0;
    const float bw = denom > 1e-6 ? (d00 * dot(v2, v1) - d01 * dot(v2, v0)) / denom : 0.0;
    const float bu = 1.0 - bv - bw;
    if (denom > 1e-6) {
        float3 Ns = n0 * bu + n1 * bv + n2 * bw;
        if (dot(Ns, Ns) > 1e-6) {
            Ns = normalize(Ns);
            if (dot(Ns, N) < 0.0) {
                Ns = -Ns;
            }
            N = Ns;
        }
    }
    if (dot(N, gFrame.cameraPos.xyz - P) < 0.0) {
        N = -N;
    }
    return N;
}

float3 albedoAt(uint instanceId, uint tri, uint2 px) {
    const GpuInstance inst = gInstances[instanceId];
    const MaterialGpu mat = gMat[inst.materialId];
    float3 albedo = mat.baseColor.rgb;
    const uint tex = mat.albedo;
    if (tex >= 768) {
        return albedo;
    }
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint at = meshlet.indexOffset + tri * 3;
    const uint i0 = meshlet.vertexOffset + gIdx[at];
    const uint i1 = meshlet.vertexOffset + gIdx[at + 1];
    const uint i2 = meshlet.vertexOffset + gIdx[at + 2];
    const float4 h0 = float4(gVerts[i0].pos, 1.0);
    const float4 h1 = float4(gVerts[i1].pos, 1.0);
    const float4 h2 = float4(gVerts[i2].pos, 1.0);
    const float3 w0 = float3(dot(inst.world0, h0), dot(inst.world1, h0), dot(inst.world2, h0));
    const float3 w1 = float3(dot(inst.world0, h1), dot(inst.world1, h1), dot(inst.world2, h1));
    const float3 w2 = float3(dot(inst.world0, h2), dot(inst.world1, h2), dot(inst.world2, h2));
    const float4 c0 = worldToClip(w0);
    const float4 c1 = worldToClip(w1);
    const float4 c2 = worldToClip(w2);
    const float2 s0 = (c0.xy / max(abs(c0.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 s1 = (c1.xy / max(abs(c1.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 s2 = (c2.xy / max(abs(c2.w), 1e-4) * 0.5 + 0.5) * float2(gFrame.extent.xy);
    const float2 sp = float2(px) + 0.5;
    const float2 v0 = s1 - s0;
    const float2 v1 = s2 - s0;
    const float2 v2 = sp - s0;
    const float d00 = dot(v0, v0);
    const float d01 = dot(v0, v1);
    const float d11 = dot(v1, v1);
    const float denom = d00 * d11 - d01 * d01;
    const float bv = denom > 1e-6 ? (d11 * dot(v2, v0) - d01 * dot(v2, v1)) / denom : 0.0;
    const float bw = denom > 1e-6 ? (d00 * dot(v2, v1) - d01 * dot(v2, v0)) / denom : 0.0;
    const float bu = 1.0 - bv - bw;
    const float2 uv = float2(
        gVerts[i0].u * bu + gVerts[i1].u * bv + gVerts[i2].u * bw,
        gVerts[i0].v * bu + gVerts[i1].v * bv + gVerts[i2].v * bw);
    return albedo * gTex[tex].SampleLevel(gSamp, uv, 0.0).rgb;
}

float4 marchInterval(float3 origin, float3 dir, float tBegin, float tEnd, bool sky, uint skipVis) {
    const uint steps = uint(clamp(int(gSsrc.steps), 1, 16));
    const float span = max(tEnd - tBegin, 0.05);
    const float step = max(span / float(steps), 0.05);
    [loop]
    for (uint s = 0; s < steps; ++s) {
        const float t = tBegin + (float(s) + 0.5) * step;
        if (t > tEnd) {
            break;
        }
        const float3 world = origin + dir * t;
        const float4 clip = worldToClip(world);
        if (clip.w <= 1e-3) {
            continue;
        }
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
            continue;
        }
        const uint2 px = uint2(uv * float2(gFrame.extent.xy));
        const uint vis = gVis.Load(int3(px, 0));
        if (vis == 0 || vis == skipVis) {
            continue;
        }
        const float sceneZ = gDepth.Load(int3(px, 0));
        if (sceneZ <= 0.0) {
            continue;
        }
        const float3 sceneP = reconstructWorld(px, sceneZ);
        const float along = dot(sceneP - origin, dir);
        const float side = length(sceneP - origin - dir * along);
        const float tube = max(gSsrc.thick, t * gFrame.invExtent.y * 2.2);
        if (along > tBegin && along <= tEnd && side < tube) {
            const uint packed = vis - 1;
            const float3 alb = albedoAt(packed & 0xFFFFF, packed >> 20, px);
            return float4(alb * 0.35, 0.0);
        }
    }
    if (sky && dir.y > 0.15) {
        return float4(0.02, 0.025, 0.035, 1.0);
    }
    return float4(0.0, 0.0, 0.0, 1.0);
}

float4 loadRay(RWTexture2D<float4> img, uint2 probe, uint2 ray, uint raySide) {
    return img.Load(int2(probe.x * raySide + ray.x, probe.y * raySide + ray.y));
}

float4 mergedFar(RWTexture2D<float4> upper, uint2 probe, uint2 ray, uint raySide, uint probesX, uint probesY) {
    const uint upperSide = raySide * 2;
    const uint upX = max(probesX / 2, 1);
    const uint upY = max(probesY / 2, 1);
    const float2 base = (float2(probe) + 0.5) * 0.5 - 0.5;
    const uint2 i0 = uint2(clamp(floor(base), float2(0.0, 0.0), float2(upX - 1, upY - 1)));
    const uint2 i1 = uint2(min(i0.x + 1, upX - 1), min(i0.y + 1, upY - 1));
    const float2 f = saturate(base - float2(i0));
    const float w00 = (1.0 - f.x) * (1.0 - f.y);
    const float w10 = f.x * (1.0 - f.y);
    const float w01 = (1.0 - f.x) * f.y;
    const float w11 = f.x * f.y;
    float4 acc = 0.0;
    [unroll]
    for (uint oy = 0; oy < 2; ++oy) {
        [unroll]
        for (uint ox = 0; ox < 2; ++ox) {
            const uint2 ur = uint2(ray.x * 2 + ox, ray.y * 2 + oy);
            const float4 s00 = loadRay(upper, uint2(i0.x, i0.y), ur, upperSide);
            const float4 s10 = loadRay(upper, uint2(i1.x, i0.y), ur, upperSide);
            const float4 s01 = loadRay(upper, uint2(i0.x, i1.y), ur, upperSide);
            const float4 s11 = loadRay(upper, uint2(i1.x, i1.y), ur, upperSide);
            acc += (s00 * w00 + s10 * w10 + s01 * w01 + s11 * w11) * 0.25;
        }
    }
    return acc;
}

void traceOne(RWTexture2D<float4> dst, RWTexture2D<float4> upper, uint cascade, uint2 pix) {
    const uint div = 4u << cascade;
    const uint raySide = 1u << (cascade + 1u);
    const uint probesX = max(gFrame.extent.x / div, 1u);
    const uint probesY = max(gFrame.extent.y / div, 1u);
    const uint2 probe = pix / raySide;
    const uint2 ray = pix % raySide;
    if (probe.x >= probesX || probe.y >= probesY || pix.x >= probesX * raySide || pix.y >= probesY * raySide) {
        dst[pix] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const uint2 spp = uint2(
        (probe.x * gFrame.extent.x) / probesX + gFrame.extent.x / (probesX * 2),
        (probe.y * gFrame.extent.y) / probesY + gFrame.extent.y / (probesY * 2));
    const uint vis = gVis.Load(int3(spp, 0));
    if (vis == 0) {
        dst[uint2(probe.x * raySide + ray.x, probe.y * raySide + ray.y)] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const uint packed = vis - 1;
    const float depth = gDepth.Load(int3(spp, 0));
    const float3 P = reconstructWorld(spp, depth);
    const float3 N = faceNormal(packed & 0xFFFFF, packed >> 20, P, spp);
    if (dot(N, N) < 1e-8) {
        dst[uint2(probe.x * raySide + ray.x, probe.y * raySide + ray.y)] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const float base = max(gSsrc.base, 0.05);
    const float tEnd = base * exp2(float(cascade) * 2.0);
    const float tBegin = cascade == 0u ? 0.05 : base * exp2(float(cascade - 1u) * 2.0);
    const float u = (float(ray.x) + 0.5) / float(raySide);
    const float v = (float(ray.y) + 0.5) / float(raySide);
    const float phi = 6.2831853 * u;
    const float cosT = v;
    const float sinT = sqrt(max(1.0 - cosT * cosT, 0.0));
    const float3 up = abs(N.y) > 0.98 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    const float3 T = normalize(cross(up, N));
    const float3 B = cross(N, T);
    const float3 dir = normalize(T * (cos(phi) * sinT) + B * (sin(phi) * sinT) + N * cosT);
    float4 near = marchInterval(P + N * 0.04, dir, tBegin, tEnd, cascade == 3u, vis);
    if (cascade < 3) {
        const float4 far = mergedFar(upper, probe, ray, raySide, probesX, probesY);
        near = float4(near.rgb + near.a * far.rgb, near.a * far.a);
    }
    dst[uint2(probe.x * raySide + ray.x, probe.y * raySide + ray.y)] = near;
}

#ifndef CASCADE
#define CASCADE 0
#endif

[[vk::binding(3, 0)]] RWTexture2D<float4> gHdrOut;
[[vk::binding(20, 0)]] RWTexture2D<float4> gDst0;
[[vk::binding(21, 0)]] RWTexture2D<float4> gDst1;
[[vk::binding(22, 0)]] RWTexture2D<float4> gUp0;
[[vk::binding(23, 0)]] RWTexture2D<float4> gUp1;

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrcCs(uint3 dtid : SV_DispatchThreadID) {
    if (gFrame.padExt.y == 0) {
        traceOne(gDst0, gUp0, CASCADE, dtid.xy);
    } else {
        traceOne(gDst1, gUp1, CASCADE, dtid.xy);
    }
}

float3 gatherProbe(RWTexture2D<float4> field, uint2 px) {
    const uint probesX = max(gFrame.extent.x / 4u, 1u);
    const uint probesY = max(gFrame.extent.y / 4u, 1u);
    const float2 fprobe = (float2(px) + 0.5) / float2(gFrame.extent.xy) * float2(probesX, probesY);
    const uint2 probe = uint2(clamp(floor(fprobe), float2(0.0, 0.0), float2(probesX - 1, probesY - 1)));
    float3 acc = 0.0;
    [unroll]
    for (uint ry = 0; ry < 2; ++ry) {
        [unroll]
        for (uint rx = 0; rx < 2; ++rx) {
            acc += loadRay(field, probe, uint2(rx, ry), 2).rgb;
        }
    }
    return acc * 0.25;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrcApply(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gFrame.debugMode == 1 || gFrame.debugMode == 2) {
        return;
    }
    if (gFrame.disableSSRC != 0 && gFrame.debugMode != 4) {
        return;
    }
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0) {
        return;
    }
    float3 gi = gFrame.padExt.y == 0 ? gatherProbe(gDst0, px) : gatherProbe(gDst1, px);
    const float cap = max(gSsrc.cap, 0.0);
    gi = min(max(gi, float3(0.0, 0.0, 0.0)), float3(cap, cap, cap)) * max(gSsrc.strength, 0.0);
    const uint packed = vis - 1;
    const float3 albedo = albedoAt(packed & 0xFFFFF, packed >> 20, px);
    if (gFrame.debugMode == 4) {
        gHdrOut[px] = float4(gi, 1.0);
        return;
    }
    float4 hdr = gHdrOut[px];
    hdr.rgb += albedo * gi;
    gHdrOut[px] = hdr;
}
