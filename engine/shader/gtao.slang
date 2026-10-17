#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(32, 0)]] RWTexture2D<float> gAo;

float depthAt(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return gDepth.Load(int3(clamp(p, int2(0, 0), e), 0));
}

float ignGtao(uint2 px) {
    return frac(52.9829189 * frac(0.06711056 * float(px.x) + 0.00583715 * float(px.y)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void gtaoCs(uint3 dtid : SV_DispatchThreadID) {
    const uint2 halfExt = max(gFrame.extent.xy >> 1, uint2(1, 1));
    if (dtid.x >= halfExt.x || dtid.y >= halfExt.y) {
        return;
    }
    const uint2 base = dtid.xy * 2u;
    const uint2 ext = gFrame.extent.xy;
    if ((gPass.passMask & 16u) == 0u || gGtao.intensity <= 0.001) {
        [unroll]
        for (int oy = 0; oy < 2; ++oy) {
            [unroll]
            for (int ox = 0; ox < 2; ++ox) {
                const int2 p = int2(base) + int2(ox, oy);
                if (p.x < int(ext.x) && p.y < int(ext.y)) {
                    gAo[p] = 1.0;
                }
            }
        }
        return;
    }
    const int2 center = int2(min(base, ext - 1u));
    const float cd = depthAt(center);
    if (cd <= 1e-6) {
        [unroll]
        for (int oy = 0; oy < 2; ++oy) {
            [unroll]
            for (int ox = 0; ox < 2; ++ox) {
                const int2 p = int2(base) + int2(ox, oy);
                if (p.x < int(ext.x) && p.y < int(ext.y)) {
                    gAo[p] = 1.0;
                }
            }
        }
        return;
    }
    const float2 cuv = (float2(center) + 0.5) * gFrame.invExtent;
    const float3 P = reconstructWorld(cuv, cd);
    const float3 toCam = gFrame.cameraPos.xyz - P;
    const float viewDist = max(length(toCam), 0.1);
    const float3 V = toCam / viewDist;
    const float3 N = reconstructNormalFromDepth(gDepth, uint2(center), P);

    // Camera-aligned screen-plane basis: invariant to world pitch/yaw/roll
    const float3 pRight = reconstructWorld(cuv + float2(gFrame.invExtent.x, 0.0), cd) - P;
    const float3 pDown  = reconstructWorld(cuv + float2(0.0, gFrame.invExtent.y), cd) - P;
    float3 screenX = pRight - V * dot(pRight, V);
    float3 screenY = pDown  - V * dot(pDown, V);
    const float lenX = length(screenX);
    const float lenY = length(screenY);
    screenX = lenX > 1e-6 ? (screenX / lenX) : normalize(cross(abs(V.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), V));
    screenY = lenY > 1e-6 ? (screenY / lenY) : normalize(cross(V, screenX));

    const float worldRad = clamp(gGtao.radius * 0.35, 0.08, 4.0);
    const float projScale = float(ext.y) * 0.5;
    const float radiusPx = clamp((worldRad / viewDist) * projScale, 3.0, 96.0);

    const int slices = clamp(int(gGtao.slices), 2, 6);
    const int steps = clamp(int(gGtao.steps), 3, 8);
    const float falloffRange = clamp(gGtao.falloff, 0.15, 1.0);
    const float invRad2 = 1.0 / max(worldRad * worldRad, 1e-4);

    const float noiseAngle = ignGtao(dtid.xy);
    const float noiseStep = frac(noiseAngle * 3.6180339887);

    float visibility = 0.0;
    [loop]
    for (int s = 0; s < slices; ++s) {
        const float phi = ((float(s) + noiseAngle) / float(slices)) * kPi;
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 sliceDir = normalize(screenX * omega.x + screenY * omega.y);
        const float3 orthoDir = sliceDir - V * dot(sliceDir, V);
        const float3 axis = normalize(cross(sliceDir, V));
        const float3 projN = N - axis * dot(N, axis);
        const float projLen = length(projN);
        if (projLen < 1e-4) {
            visibility += 1.0;
            continue;
        }
        const float cosNorm = clamp(dot(projN / projLen, V), -1.0, 1.0);
        const float signNorm = dot(orthoDir, projN) >= 0.0 ? 1.0 : -1.0;
        const float n = signNorm * fastAcos(cosNorm);

        const float low0 = sin(n);
        const float low1 = -sin(n);
        float hcos0 = -1.0;
        float hcos1 = -1.0;

        [loop]
        for (int k = 1; k <= steps; ++k) {
            const float sFrac = (float(k) - 0.5 + (noiseStep - 0.5) * 0.5) / float(steps);
            const float stepPx = max(sFrac * radiusPx, float(k) * 1.25);
            const float2 uvOffset = omega * stepPx * gFrame.invExtent;

            const float2 uv0 = cuv + uvOffset;
            if (uv0.x > 0.0 && uv0.y > 0.0 && uv0.x < 1.0 && uv0.y < 1.0) {
                const int2 sp0 = int2(uv0 * float2(ext));
                const float sd0 = depthAt(sp0);
                if (sd0 > 1e-6) {
                    const float3 delta = reconstructWorld((float2(sp0) + 0.5) * gFrame.invExtent, sd0) - P;
                    const float dist2 = dot(delta, delta);
                    if (dist2 > 1e-6) {
                        const float invDist = rsqrt(dist2);
                        const float dist = dist2 * invDist;
                        const float shc = dot(delta * invDist, V);
                        const float falloff = saturate((worldRad - dist) / max(worldRad * falloffRange, 1e-3));
                        const float thinHeuristic = saturate(1.0 - dist2 * invRad2 * 0.5);
                        hcos0 = max(hcos0, lerp(low0, shc, falloff * thinHeuristic));
                    }
                }
            }

            const float2 uv1 = cuv - uvOffset;
            if (uv1.x > 0.0 && uv1.y > 0.0 && uv1.x < 1.0 && uv1.y < 1.0) {
                const int2 sp1 = int2(uv1 * float2(ext));
                const float sd1 = depthAt(sp1);
                if (sd1 > 1e-6) {
                    const float3 delta = reconstructWorld((float2(sp1) + 0.5) * gFrame.invExtent, sd1) - P;
                    const float dist2 = dot(delta, delta);
                    if (dist2 > 1e-6) {
                        const float invDist = rsqrt(dist2);
                        const float dist = dist2 * invDist;
                        const float shc = dot(delta * invDist, V);
                        const float falloff = saturate((worldRad - dist) / max(worldRad * falloffRange, 1e-3));
                        const float thinHeuristic = saturate(1.0 - dist2 * invRad2 * 0.5);
                        hcos1 = max(hcos1, lerp(low1, shc, falloff * thinHeuristic));
                    }
                }
            }
        }

        const float h0 = -fastAcos(clamp(hcos1, -1.0, 1.0));
        const float h1 =  fastAcos(clamp(hcos0, -1.0, 1.0));
        const float h0c = clamp(h0, n - 1.5707963, n + 1.5707963);
        const float h1c = clamp(h1, n - 1.5707963, n + 1.5707963);
        const float iarc0 = (cosNorm + 2.0 * h0c * sin(n) - cos(2.0 * h0c - n)) * 0.25;
        const float iarc1 = (cosNorm + 2.0 * h1c * sin(n) - cos(2.0 * h1c - n)) * 0.25;
        visibility += projLen * (iarc0 + iarc1);
    }

    const float rawVis = saturate(visibility / float(slices));
    const float ao = clamp(pow(rawVis, max(gGtao.power, 0.1) * max(gGtao.intensity, 0.0)), 0.04, 1.0);

    [unroll]
    for (int oy = 0; oy < 2; ++oy) {
        [unroll]
        for (int ox = 0; ox < 2; ++ox) {
            const int2 p = int2(base) + int2(ox, oy);
            if (p.x >= int(ext.x) || p.y >= int(ext.y)) {
                continue;
            }
            const float sd = depthAt(p);
            if (sd <= 1e-6) {
                gAo[p] = 1.0;
                continue;
            }
            const float3 pWorld = reconstructWorld((float2(p) + 0.5) * gFrame.invExtent, sd);
            const float planeDist = abs(dot(pWorld - P, N)) / max(viewDist, 0.2);
            const float w = saturate(1.0 - planeDist * 25.0);
            gAo[p] = lerp(1.0, ao, w);
        }
    }
}