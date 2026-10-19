#include "frame.slang"
#include "camera.slang"

[[vk::binding(15, 0)]] Texture2D<float> gShadow0;

#include "shade_shadow.slang"
#include "core/brdf.slang"
#include "vx_common.slang"

[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(17, 0)]] RWStructuredBuffer<uint> gAtom;
[[vk::binding(24, 0)]] SamplerState gWrap;

float3 vxPoint(GpuInstance inst, float3 p) {
    return float3(
        dot(inst.world0, float4(p, 1.0)),
        dot(inst.world1, float4(p, 1.0)),
        dot(inst.world2, float4(p, 1.0)));
}

float3 vxNrm(GpuInstance inst, float3 n) {
    const float3 r = float3(dot(inst.world0.xyz, n), dot(inst.world1.xyz, n), dot(inst.world2.xyz, n));
    return dot(r, r) > 1e-12 ? normalize(r) : float3(0.0, 1.0, 0.0);
}

float vxSun(float3 world) {
    if (gFrame.disableShadow != 0u) {
        return 1.0;
    }
    if (cascadeOutside(0u, world) <= 0.0) {
        return cascadeLit(0u, world);
    }
    if (cascadeOutside(1u, world) <= 0.0) {
        return cascadeLit(1u, world);
    }
    return cascadeLit(2u, world);
}

void vxDeposit(uint idx, float3 rad) {
    const uint3 q = uint3(clamp(rad, 0.0, 1.0) * 255.0 + 0.5);
    uint prev;
    InterlockedMax(gAtom[idx], q.x, prev);
    InterlockedMax(gAtom[idx + 1u], q.y, prev);
    InterlockedMax(gAtom[idx + 2u], q.z, prev);
    InterlockedMax(gAtom[idx + 3u], 1u, prev);
}

void vxWrite(VxCascade cas, int3 w, float3 rad) {
    if (!vxDirty(cas, w.x, w.y, w.z)) {
        return;
    }
    const int3 local = int3(w.x - cas.anchorX, w.y - cas.anchorY, w.z - cas.anchorZ);
    const int3 tex = (local + vxScroll(cas)) & int(kVxRes - 1u);
    const uint idx = vxAtomBase() + uint(tex.x + tex.y * int(kVxRes) + tex.z * int(kVxRes * kVxRes)) * 4u;
    vxDeposit(idx, rad);
}

float vxEdge(float2 p, float2 a, float2 b) {
    const float2 e = b - a;
    const float len = length(e);
    if (len < 1e-5) {
        return -1e6;
    }
    const float2 n = float2(-e.y, e.x) / len;
    return dot(p - a, n);
}

bool vxCover(float2 p, float2 a, float2 b, float2 c) {
    float e0 = vxEdge(p, a, b);
    float e1 = vxEdge(p, b, c);
    float e2 = vxEdge(p, c, a);
    if (e0 + e1 + e2 < 0.0) {
        e0 = -e0;
        e1 = -e1;
        e2 = -e2;
    }
    return e0 >= -0.75 && e1 >= -0.75 && e2 >= -0.75;
}

bool vxRange(int lo, int hi, int smin, int smax) {
    return smax > smin && hi >= smin && lo < smax;
}

bool vxTriDirty(VxCascade cas, int3 lo, int3 hi) {
    if (cas.full != 0u) {
        return true;
    }
    const bool volX = hi.x >= cas.anchorX && lo.x < cas.anchorX + int(kVxRes);
    const bool volY = hi.y >= cas.anchorY && lo.y < cas.anchorY + int(kVxRes);
    const bool volZ = hi.z >= cas.anchorZ && lo.z < cas.anchorZ + int(kVxRes);
    if (vxRange(lo.x, hi.x + 1, cas.slabMinX, cas.slabMaxX) && volY && volZ) {
        return true;
    }
    if (vxRange(lo.y, hi.y + 1, cas.slabMinY, cas.slabMaxY) && volX && volZ) {
        return true;
    }
    if (vxRange(lo.z, hi.z + 1, cas.slabMinZ, cas.slabMaxZ) && volX && volY) {
        return true;
    }
    return false;
}

float3 vxBary(float2 p, float2 pa, float2 pb, float2 pc, out float wA, out float wB, out float wC) {
    float e0 = vxEdge(p, pa, pb);
    float e1 = vxEdge(p, pb, pc);
    float e2 = vxEdge(p, pc, pa);
    float sum = e0 + e1 + e2;
    if (sum < 0.0) {
        e0 = -e0;
        e1 = -e1;
        e2 = -e2;
        sum = -sum;
    }
    sum = max(sum, 1e-5);
    wA = e1 / sum;
    wB = e2 / sum;
    wC = e0 / sum;
    return float3(wA, wB, wC);
}

float3 vxPbr(VxCascade cas, float3 world, float2 p, float2 pa, float2 pb, float2 pc, float3 a, float3 b, float3 c, float2 uv0, float2 uv1, float2 uv2, float3 n0, float3 n1, float3 n2, float3 face, float3 L, float3 sunCol, MaterialGpu mat, float3 lamps) {
    float wA;
    float wB;
    float wC;
    vxBary(p, pa, pb, pc, wA, wB, wC);
    const float2 uv = uv0 * wA + uv1 * wB + uv2 * wC;
    const float lod = cas.voxel <= 0.2 ? 0.0 : log2(max(cas.voxel / 0.125, 1.0));
    float3 albedo = max(mat.baseColor.rgb, 0.0);
    if (mat.albedo < 768u) {
        const float4 texel = gTex[NonUniformResourceIndex(mat.albedo)].SampleLevel(gWrap, uv, lod);
        if ((mat.flags & 32u) != 0u && texel.a < 0.4) {
            return 0.0;
        }
        albedo *= max(texel.rgb, 0.0);
    }
    float rough = clamp(mat.roughness, 0.04, 1.0);
    float metal = saturate(mat.metallic);
    if ((mat.flags & 4u) != 0u && mat.orm > 0u && mat.orm < 768u) {
        const float3 orm = gTex[NonUniformResourceIndex(mat.orm)].SampleLevel(gWrap, uv, lod).rgb;
        rough = clamp(orm.g, 0.04, 1.0);
        metal = saturate(orm.b);
    }
    float3 n = n0 * wA + n1 * wB + n2 * wC;
    if (dot(n, n) > 1e-6) {
        n = normalize(n);
    } else {
        n = face;
    }
    if (dot(n, face) < 0.0) {
        n = -n;
    }
    if ((mat.flags & 32u) == 0u && mat.normalTex > 0u && mat.normalTex < 768u) {
        const float2 d1 = uv1 - uv0;
        const float2 d2 = uv2 - uv0;
        const float det = d1.x * d2.y - d2.x * d1.y;
        if (abs(det) > 1e-8) {
            const float invDet = 1.0 / det;
            float3 T = ((b - a) * d2.y - (c - a) * d1.y) * invDet;
            T = T - n * dot(n, T);
            if (dot(T, T) > 1e-8) {
                T = normalize(T);
                const float3 B = cross(n, T) * (det < 0.0 ? -1.0 : 1.0);
                const float2 rg = gTex[NonUniformResourceIndex(mat.normalTex)].SampleLevel(gWrap, uv, lod).rg * 2.0 - 1.0;
                const float nz = sqrt(max(1.0 - dot(rg, rg), 0.0));
                n = normalize(T * rg.x + B * rg.y + n * nz);
            }
        }
    }
    const float ndotl = saturate(dot(n, L));
    const float lobe = max(ndotl, hammonDiffuse(ndotl, 1.0, ndotl, rough) * kPi);
    const float luma = dot(albedo, float3(0.299, 0.587, 0.114));
    const float3 sat = max(lerp(float3(luma, luma, luma), albedo, 1.25), 0.0);
    const float shadow = ndotl > 0.0 ? vxSun(world) : 0.0;
    float3 glow = sat * mat.emissive * 0.5;
    if (mat.emissiveTex > 0u && mat.emissiveTex < 768u) {
        glow *= gTex[NonUniformResourceIndex(mat.emissiveTex)].SampleLevel(gWrap, uv, lod).rgb;
    }
    return sat * (1.0 - metal * 0.85) * lobe * sunCol * shadow + glow + lamps;
}

void vxRaster(VxCascade cas, float3 a, float3 b, float3 c, float2 uv0, float2 uv1, float2 uv2, float3 n0, float3 n1, float3 n2, MaterialGpu mat, float3 lamps) {
    const float3 e1 = b - a;
    const float3 e2 = c - a;
    float3 face = cross(e1, e2);
    if (dot(face, face) < 1e-12) {
        return;
    }
    face = normalize(face);
    const float3 nAvg = n0 + n1 + n2;
    if (dot(face, nAvg) < 0.0) {
        face = -face;
    }
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    const float3 L = dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
    float3 sunCol = gSun.sunColor.xyz;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.92, 0.76);
    }
    const float3 va = a / cas.voxel;
    const float3 vb = b / cas.voxel;
    const float3 vc = c / cas.voxel;
    const float3 vmin = min(va, min(vb, vc));
    const float3 vmax = max(va, max(vb, vc));
    int3 lo = int3(floor(vmin)) - 1;
    int3 hi = int3(floor(vmax)) + 1;
    const int3 volLo = vxAnchor(cas);
    const int3 volHi = volLo + int(kVxRes) - 1;
    lo = max(lo, volLo);
    hi = min(hi, volHi);
    if (any(lo > hi) || !vxTriDirty(cas, lo, hi)) {
        return;
    }
    const float3 an = abs(face);
    uint axis = 0u;
    if (an.y > an.x) {
        axis = 1u;
    }
    if (an.z > an[axis]) {
        axis = 2u;
    }
    const uint u = (axis + 1u) % 3u;
    const uint v = (axis + 2u) % 3u;
    const float plane = dot(face, a);
    if (abs(face[axis]) < 1e-5) {
        return;
    }
    const int area = (hi[u] - lo[u] + 1) * (hi[v] - lo[v] + 1);
    const int step = area > 8192 ? 2 : 1;
    [loop]
    for (int iv = lo[v]; iv <= hi[v]; iv += step) {
        [loop]
        for (int iu = lo[u]; iu <= hi[u]; iu += step) {
            const float2 p = float2(float(iu) + 0.5, float(iv) + 0.5);
            const float2 pa = float2(va[u], va[v]);
            const float2 pb = float2(vb[u], vb[v]);
            const float2 pc = float2(vc[u], vc[v]);
            if (!vxCover(p, pa, pb, pc)) {
                continue;
            }
            float3 world = 0.0;
            world[u] = (float(iu) + 0.5) * cas.voxel;
            world[v] = (float(iv) + 0.5) * cas.voxel;
            world[axis] = (plane - face[u] * world[u] - face[v] * world[v]) / face[axis];
            const int iw = int(floor(world[axis] / cas.voxel));
            const float3 rad = vxPbr(cas, world, p, pa, pb, pc, a, b, c, uv0, uv1, uv2, n0, n1, n2, face, L, sunCol, mat, lamps);
            [unroll]
            for (int s = 0; s < step; ++s) {
                [unroll]
                for (int t = 0; t < step; ++t) {
                    int3 cell;
                    cell[u] = iu + s;
                    cell[v] = iv + t;
                    cell[axis] = iw;
                    vxWrite(cas, cell, rad);
                    cell[axis] = iw + 1;
                    vxWrite(cas, cell, rad);
                }
            }
        }
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void vxVoxel(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gPush.instances) {
        return;
    }
    const GpuInstance inst = gInstances[dtid.x];
    if (inst.pad.x != 0u) {
        return;
    }
    const VxCascade cas = vxLevel(gPush.cascade);
    const float3 center = inst.sphere.xyz;
    const float radius = inst.sphere.w;
    const float3 bmin = float3(cas.anchorX, cas.anchorY, cas.anchorZ) * cas.voxel;
    const float3 bmax = bmin + cas.extent;
    if (center.x + radius < bmin.x || center.y + radius < bmin.y || center.z + radius < bmin.z
        || center.x - radius > bmax.x || center.y - radius > bmax.y || center.z - radius > bmax.z) {
        return;
    }
    const MeshletRef mesh = gMeshlets[inst.meshId];
    const uint tris = min(mesh.triangleCount, 512u);
    if (tris == 0u || inst.materialId >= 4096u) {
        return;
    }
    const MaterialGpu mat = gMat[inst.materialId];
    [loop]
    for (uint t = 0u; t < tris; ++t) {
        const uint at = mesh.indexOffset + t * 3u;
        const uint i0 = gIdx[at];
        const uint i1 = gIdx[at + 1u];
        const uint i2 = gIdx[at + 2u];
        if (i0 >= mesh.vertexCount || i1 >= mesh.vertexCount || i2 >= mesh.vertexCount) {
            continue;
        }
        const Vertex v0 = gVerts[mesh.vertexOffset + i0];
        const Vertex v1 = gVerts[mesh.vertexOffset + i1];
        const Vertex v2 = gVerts[mesh.vertexOffset + i2];
        const float3 a = vxPoint(inst, v0.pos);
        const float3 b = vxPoint(inst, v1.pos);
        const float3 c = vxPoint(inst, v2.pos);
        const float2 uv0 = float2(v0.u, v0.v);
        const float2 uv1 = float2(v1.u, v1.v);
        const float2 uv2 = float2(v2.u, v2.v);
        const float2 uv = (uv0 + uv1 + uv2) * (1.0 / 3.0);
        float3 albedo = max(mat.baseColor.rgb, 0.0);
        if (mat.albedo < 768u) {
            const float4 texel = gTex[NonUniformResourceIndex(mat.albedo)].SampleLevel(gWrap, uv, 0.0);
            if ((mat.flags & 32u) != 0u && texel.a < 0.4) {
                continue;
            }
            albedo *= max(texel.rgb, 0.0);
        }
        float3 lamps = albedo * mat.emissive * 0.5;
        const float3 mid = (a + b + c) * (1.0 / 3.0);
        const float3 n0 = vxNrm(inst, v0.nrm);
        const float3 n1 = vxNrm(inst, v1.nrm);
        const float3 n2 = vxNrm(inst, v2.nrm);
        const float3 n = normalize(n0 + n1 + n2 + float3(1e-4, 1e-4, 1e-4));
        [unroll]
        for (uint li = 0u; li < 3u; ++li) {
            const float3 lc = gSun.pointColor[li].xyz;
            if (dot(lc, lc) < 1e-4) {
                continue;
            }
            const float3 d = gSun.pointPos[li].xyz - mid;
            const float dist2 = max(dot(d, d), 0.05);
            const float nd = saturate(dot(n, d * rsqrt(dist2)));
            lamps += albedo * lc * nd / (1.0 + dist2) * 0.15;
        }
        vxRaster(cas, a, b, c, uv0, uv1, uv2, n0, n1, n2, mat, lamps);
    }
}
