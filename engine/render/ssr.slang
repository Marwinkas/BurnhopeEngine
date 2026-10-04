#include "frame.slang"
#include "camera.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(4, 0)]] Texture2D<float4> gHdrA;
[[vk::binding(5, 0)]] RWTexture2D<float4> gHdrB;
[[vk::binding(6, 0)]] Texture2D<float> gHiZ;
[[vk::binding(7, 0)]] Texture2D<float4> gCube;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(11, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(12, 0)]] RWTexture2D<float4> gHalf;
[[vk::binding(1, 1)]] SamplerState gLinear;

uint mipY(uint mip) {
    uint y = 0;
    uint h = gFrame.extent.y;
    for (uint i = 0; i < mip; ++i) {
        y += h;
        h = max(h >> 1, 1u);
    }
    return y;
}

float hizAt(float2 uv, uint mip) {
    const uint2 ext = max(gFrame.extent.xy >> mip, uint2(1, 1));
    const uint2 p = uint2(clamp(uv, 0.0, 0.999) * float2(ext));
    const int3 at = int3(int(p.x), int(mipY(mip) + p.y), 0);
    return gHiZ.Load(at);
}

float depthLoad(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return gDepth.Load(int3(clamp(p, int2(0, 0), e), 0));
}

float3 meshNormal(uint vis, uint2 px, float3 V) {
    const uint packed = vis - 1u;
    const GpuInstance inst = gInstances[packed & 0xFFFFF];
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint at = meshlet.indexOffset + (packed >> 20) * 3u;
    const uint i0 = meshlet.vertexOffset + gIdx[at];
    const uint i1 = meshlet.vertexOffset + gIdx[at + 1u];
    const uint i2 = meshlet.vertexOffset + gIdx[at + 2u];
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
    const float d20 = dot(v2, v0);
    const float d21 = dot(v2, v1);
    const float denom = d00 * d11 - d01 * d01;
    const float bv = denom > 1e-6 ? (d11 * d20 - d01 * d21) / denom : 0.0;
    const float bw = denom > 1e-6 ? (d00 * d21 - d01 * d20) / denom : 0.0;
    const float bu = 1.0 - bv - bw;
    if (bu >= -0.2 && bv >= -0.2 && bw >= -0.2) {
        float3 Ns = n0 * bu + n1 * bv + n2 * bw;
        if (dot(Ns, Ns) > 1e-6) {
            Ns = normalize(Ns);
            if (dot(Ns, N) < 0.0) {
                Ns = -Ns;
            }
            N = Ns;
        }
    }
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    return N;
}

float3 boxReflect(float3 pos, float3 dir) {
    const float3 bmin = gFrame.cubeMin.xyz;
    const float3 bmax = gFrame.cubeMax.xyz;
    const float3 inv = 1.0 / dir;
    const float3 t0 = (bmin - pos) * inv;
    const float3 t1 = (bmax - pos) * inv;
    const float3 tsm = min(t0, t1);
    const float3 tbg = max(t0, t1);
    const float t = min(tbg.x, min(tbg.y, tbg.z));
    const float3 hit = pos + dir * max(t, 0.05);
    return normalize(hit - (bmin + bmax) * 0.5);
}

float3 sampleCube(float3 dir) {
    const float3 a = abs(dir);
    uint face = 0;
    float2 uv = 0.0;
    if (a.x >= a.y && a.x >= a.z) {
        face = dir.x >= 0.0 ? 0u : 1u;
        uv = float2(dir.x >= 0.0 ? -dir.z : dir.z, -dir.y) / a.x;
    } else if (a.y >= a.z) {
        face = dir.y >= 0.0 ? 2u : 3u;
        uv = float2(dir.x, dir.y >= 0.0 ? dir.z : -dir.z) / a.y;
    } else {
        face = dir.z >= 0.0 ? 4u : 5u;
        uv = float2(dir.z >= 0.0 ? dir.x : -dir.x, -dir.y) / a.z;
    }
    uv = uv * 0.5 + 0.5;
    return gCube.SampleLevel(gLinear, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrCs(uint3 dtid : SV_DispatchThreadID) {
    const uint2 halfExt = max(gFrame.extent.xy >> 1, uint2(1, 1));
    if (dtid.x >= halfExt.x || dtid.y >= halfExt.y) {
        return;
    }
    const uint2 half = dtid.xy;
    const uint2 px = min(half * 2u + uint2(1, 1), gFrame.extent.xy - 1u);
    const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
    const float4 lit = gHdrA.SampleLevel(gLinear, uv, 0);
    const bool rawSsr = gFrame.debugMode == 3;
    if (!rawSsr && (gFrame.disableSSR != 0 || gFrame.debugMode != 0)) {
        gHalf[half] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0) {
        gHalf[half] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(uv, depth);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    float3 N = meshNormal(vis, px, V);
    const float rough = saturate(lit.a);
    const float gloss = saturate(1.0 - rough);
    const float reflW = gloss * gloss;
    if (rough > gSsr.cutoff) {
        gHalf[half] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float3 R = reflect(-V, N);
    bool found = false;
    float3 hit = 0.0;
    const float distP = max(length(P - gFrame.cameraPos.xyz), 0.5);
    const float rayMax = max(gSsr.maxDist, 1.0);
    const float stride = clamp(max(max(gSsr.bias, 0.02), distP * max(gSsr.stride, 0.001)), 0.02, rayMax);
    float t = stride;
    uint mip = 5;
    const int stepN = clamp(int(gSsr.steps), 4, 96);
    [loop]
    for (int i = 0; i < stepN; ++i) {
        if (t > rayMax) {
            break;
        }
        const float3 world = P + R * t;
        const float4 clip = worldToClip(world);
        if (clip.w <= 1e-3) {
            break;
        }
        const float2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x <= 0.0 || suv.y <= 0.0 || suv.x >= 1.0 || suv.y >= 1.0) {
            break;
        }
        const float sceneZ = hizAt(suv, mip);
        const float rayZ = clip.z / clip.w;
        const float cell = exp2(float(mip));
        const float eps = max(0.00004, sceneZ * max(gSsr.thick, 0.001));
        if (rayZ < sceneZ - eps) {
            const float edge = max(abs(hizAt(suv + gFrame.invExtent, 0) - sceneZ), abs(hizAt(suv - gFrame.invExtent, 0) - sceneZ));
            if (mip == 0 && rayZ > sceneZ - eps * 5.0 && edge < max(0.02, eps * 8.0) && sceneZ > 0.0) {
                hit = gHdrA.SampleLevel(gLinear, suv, 0.0).rgb;
                found = true;
                break;
            }
            mip = mip > 0 ? mip - 1 : 0;
        } else {
            t += max(cell * stride * 0.2, stride * 0.5);
            mip = min(mip + 1, 5);
        }
    }
    if (rawSsr) {
        gHalf[half] = float4(found ? min(hit, float3(1.0, 1.0, 1.0)) : float3(0.0, 0.0, 0.0), 1.0);
        return;
    }
    if (!found) {
        gHalf[half] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    const float ndv = saturate(dot(N, V));
    float occ = 0.0;
    [unroll]
    for (int s = 0; s < 4; ++s) {
        const int2 sp = int2(px) + int2((s & 1) == 0 ? 4 : 0, (s & 2) == 0 ? 0 : 4) * (s < 2 ? 1 : -1);
        const float sd = depthLoad(sp);
        if (sd <= 0.001) {
            continue;
        }
        const float3 Q = reconstructWorld((float2(sp) + 0.5) * gFrame.invExtent, sd);
        const float3 delta = Q - P;
        const float len2 = dot(delta, delta);
        if (len2 > 1e-6 && len2 < 0.45 * 0.45) {
            occ = max(occ, saturate(dot(N, delta * rsqrt(len2))));
        }
    }
    const float gtao = saturate(1.0 - occ);
    const float specOccl = saturate(pow(ndv + gtao, exp2(-16.0 * rough - 1.0)) - 1.0 + gtao);
    const float F = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
    gHalf[half] = float4(min(hit, float3(1.0, 1.0, 1.0)) * F * reflW * specOccl, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void ssrUp(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gFrame.disableSSR != 0 && gFrame.debugMode != 3) {
        gHdrB[px] = float4(gHdrA.Load(int3(px, 0)).rgb, 1.0);
        return;
    }
    const float3 fullRes = gFrame.debugMode == 3 ? float3(0.0, 0.0, 0.0) : gHdrA.Load(int3(px, 0)).rgb;
    if ((gPass.passMask & 4u) == 0u) {
        const uint2 halfExtN = max(gFrame.extent.xy >> 1, uint2(1, 1));
        const uint2 h = min(px >> 1, halfExtN - 1u);
        gHdrB[px] = float4(fullRes + gHalf.Load(int2(h)).rgb, 1.0);
        return;
    }
    const uint2 halfExt = max(gFrame.extent.xy >> 1, uint2(1, 1));
    const float depth = gDepth.Load(int3(px, 0));
    const float2 p = (float2(px) + 0.5) * 0.5 - 0.5;
    const int2 base = int2(floor(p));
    const float2 f = p - float2(base);
    float3 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int oy = 0; oy <= 1; ++oy) {
        [unroll]
        for (int ox = 0; ox <= 1; ++ox) {
            const int2 h = clamp(base + int2(ox, oy), int2(0, 0), int2(halfExt) - int2(1, 1));
            const float3 c = gHalf.Load(h).rgb;
            const uint2 src = min(uint2(h) * 2u + uint2(1, 1), gFrame.extent.xy - 1u);
            const float sd = gDepth.Load(int3(src, 0));
            const float3 n0 = reconstructWorld((float2(px) + 0.5) * gFrame.invExtent, depth);
            const float3 n1 = reconstructWorld((float2(src) + 0.5) * gFrame.invExtent, sd);
            const float3 t0 = reconstructWorld((float2(px + uint2(1, 0)) + 0.5) * gFrame.invExtent, depthLoad(int2(px) + int2(1, 0)));
            const float3 t1 = reconstructWorld((float2(src + uint2(1, 0)) + 0.5) * gFrame.invExtent, depthLoad(int2(src) + int2(1, 0)));
            const float3 b0 = reconstructWorld((float2(px + uint2(0, 1)) + 0.5) * gFrame.invExtent, depthLoad(int2(px) + int2(0, 1)));
            const float3 b1 = reconstructWorld((float2(src + uint2(0, 1)) + 0.5) * gFrame.invExtent, depthLoad(int2(src) + int2(0, 1)));
            float3 N0 = cross(t0 - n0, b0 - n0);
            float3 N1 = cross(t1 - n1, b1 - n1);
            N0 = dot(N0, N0) > 1e-12 ? normalize(N0) : float3(0.0, 1.0, 0.0);
            N1 = dot(N1, N1) > 1e-12 ? normalize(N1) : float3(0.0, 1.0, 0.0);
            const float wx = ox == 0 ? (1.0 - f.x) : f.x;
            const float wy = oy == 0 ? (1.0 - f.y) : f.y;
            const float w = wx * wy * exp(-abs(sd - depth) * 40.0) * saturate(dot(N0, N1));
            acc += c * w;
            wsum += w;
        }
    }
    gHdrB[px] = float4(fullRes + (wsum > 1e-4 ? acc / wsum : float3(0.0, 0.0, 0.0)), 1.0);
}
