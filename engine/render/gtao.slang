#include "frame.slang"
#include "camera.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(32, 0)]] RWTexture2D<float> gAo;

float fastAcos(float x) {
    x = clamp(x, -1.0, 1.0);
    const float ax = abs(x);
    const float res = (-0.156583 * ax + 1.570796) * sqrt(max(1.0 - ax, 0.0));
    return x >= 0.0 ? res : 3.141593 - res;
}

float depthAt(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return gDepth.Load(int3(clamp(p, int2(0, 0), e), 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void gtaoCs(uint3 dtid : SV_DispatchThreadID) {
    const uint2 halfExt = max(gFrame.extent.xy >> 1, uint2(1, 1));
    if (dtid.x >= halfExt.x || dtid.y >= halfExt.y) {
        return;
    }
    if ((gPass.passMask & 16u) == 0u || gGtao.intensity <= 0.001) {
        const uint2 base0 = dtid.xy * 2u;
        const uint2 ext0 = gFrame.extent.xy;
        [unroll]
        for (int oy = 0; oy < 2; ++oy) {
            [unroll]
            for (int ox = 0; ox < 2; ++ox) {
                const int2 p = int2(base0) + int2(ox, oy);
                if (p.x < int(ext0.x) && p.y < int(ext0.y)) {
                    gAo[p] = 1.0;
                }
            }
        }
        return;
    }
    const uint2 base = dtid.xy * 2u;
    const uint2 ext = gFrame.extent.xy;
    const int2 center = int2(min(base + uint2(1, 1), ext - 1u));
    const float cd = depthAt(center);
    if (cd <= 0.0) {
        gAo[int2(base)] = 1.0;
        gAo[int2(base) + int2(1, 0)] = 1.0;
        gAo[int2(base) + int2(0, 1)] = 1.0;
        gAo[int2(base) + int2(1, 1)] = 1.0;
        return;
    }
    const float2 cuv = (float2(center) + 0.5) * gFrame.invExtent;
    const float3 P = reconstructWorld(cuv, cd);
    const float rad = max(gGtao.radius, 0.05);
    const float edge2 = rad * rad;
    float3 px = reconstructWorld((float2(center + int2(1, 0)) + 0.5) * gFrame.invExtent, depthAt(center + int2(1, 0))) - P;
    float3 py = reconstructWorld((float2(center + int2(0, 1)) + 0.5) * gFrame.invExtent, depthAt(center + int2(0, 1))) - P;
    if (dot(px, px) > edge2) {
        px = float3(0.0, 0.0, 0.0);
    }
    if (dot(py, py) > edge2) {
        py = float3(0.0, 0.0, 0.0);
    }
    float3 N = cross(px, py);
    N = dot(N, N) > 1e-12 ? normalize(N) : float3(0.0, 1.0, 0.0);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const int slices = clamp(int(gGtao.slices), 1, 8);
    const int steps = clamp(int(gGtao.steps), 1, 12);
    const float fallRange = clamp(gGtao.falloff, 0.05, 1.0);
    const float3 viewVec = V;
    float3 side = cross(float3(0.0, 1.0, 0.0), viewVec);
    if (dot(side, side) < 1e-6) {
        side = cross(float3(1.0, 0.0, 0.0), viewVec);
    }
    side = normalize(side);
    const float3 up = cross(viewVec, side);
    float visibility = 0.0;
    [loop]
    for (int s = 0; s < slices; ++s) {
        const float phi = (float(s) + 0.5) / float(slices) * 3.14159265;
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 sliceDir = side * omega.x + up * omega.y;
        const float3 axis = normalize(cross(sliceDir, viewVec));
        const float3 projN = N - axis * dot(N, axis);
        const float projLen = length(projN);
        if (projLen < 1e-4) {
            visibility += 1.0;
            continue;
        }
        const float3 projDir = projN / projLen;
        const float cosNorm = saturate(dot(projDir, viewVec));
        const float signNorm = dot(sliceDir, projN) >= 0.0 ? 1.0 : -1.0;
        const float n = signNorm * fastAcos(cosNorm);
        const float low0 = cos(n + 1.5707963);
        const float low1 = cos(n - 1.5707963);
        float hcos0 = low0;
        float hcos1 = low1;
        [loop]
        for (int k = 1; k <= steps; ++k) {
            const float distM = rad * (float(k) - 0.5) / float(steps);
            const float4 c0 = worldToClip(P + sliceDir * distM);
            const float4 c1 = worldToClip(P - sliceDir * distM);
            const int2 sp0 = c0.w > 1e-3 ? int2((c0.xy / c0.w * 0.5 + 0.5) * float2(ext)) : int2(-1, -1);
            const int2 sp1 = c1.w > 1e-3 ? int2((c1.xy / c1.w * 0.5 + 0.5) * float2(ext)) : int2(-1, -1);
            if (sp0.x >= 0 && sp0.y >= 0 && sp0.x < int(ext.x) && sp0.y < int(ext.y)) {
                const float sd = depthAt(sp0);
                if (sd > 0.0) {
                    const float3 delta = reconstructWorld((float2(sp0) + 0.5) * gFrame.invExtent, sd) - P;
                    const float dist = length(delta);
                    if (dist > 1e-4 && dist <= rad) {
                        const float weight = saturate((rad - dist) / max(rad * fallRange, 1e-3));
                        const float shc = dot(delta / dist, viewVec);
                        hcos0 = max(hcos0, lerp(low0, shc, weight));
                    }
                }
            }
            if (sp1.x >= 0 && sp1.y >= 0 && sp1.x < int(ext.x) && sp1.y < int(ext.y)) {
                const float sd = depthAt(sp1);
                if (sd > 0.0) {
                    const float3 delta = reconstructWorld((float2(sp1) + 0.5) * gFrame.invExtent, sd) - P;
                    const float dist = length(delta);
                    if (dist > 1e-4 && dist <= rad) {
                        const float weight = saturate((rad - dist) / max(rad * fallRange, 1e-3));
                        const float shc = dot(delta / dist, viewVec);
                        hcos1 = max(hcos1, lerp(low1, shc, weight));
                    }
                }
            }
        }
        const float h0 = -fastAcos(clamp(hcos1, -1.0, 1.0));
        const float h1 = fastAcos(clamp(hcos0, -1.0, 1.0));
        const float iarc0 = (cosNorm + 2.0 * h0 * sin(n) - cos(2.0 * h0 - n)) * 0.25;
        const float iarc1 = (cosNorm + 2.0 * h1 * sin(n) - cos(2.0 * h1 - n)) * 0.25;
        visibility += projLen * (iarc0 + iarc1);
    }
    const float ao = max(pow(saturate(visibility / float(slices)), max(gGtao.power, 0.05)), 0.03);
    [unroll]
    for (int oy = 0; oy < 2; ++oy) {
        [unroll]
        for (int ox = 0; ox < 2; ++ox) {
            const int2 p = int2(base) + int2(ox, oy);
            if (p.x >= int(ext.x) || p.y >= int(ext.y)) {
                continue;
            }
            const float sd = depthAt(p);
            const float w = exp(-abs(sd - cd) * 40.0);
            gAo[p] = lerp(1.0, ao, w);
        }
    }
}
