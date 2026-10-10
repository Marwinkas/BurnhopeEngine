// Отражение — куб мира вокруг камеры, не марш по пикселям кадра.
// Куб уже держит геометрию сцены. Сюда она подмешивается, а не прибавляется яркостью.

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(16, 0)]] Texture2D<float4> gCube;
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(36, 0)]] RWTexture2D<float4> gReflect;

float3 sampleCube(float3 dir) {
    uint face = 0u;
    float2 uv = 0.0;
    cubeDirToFaceUV(dir, face, uv);
    return gCube.SampleLevel(gWrap, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

float3 sampleWorld(float3 dir, float rough) {
    const float3 d = dot(dir, dir) > 1e-8 ? normalize(dir) : float3(0.0, 1.0, 0.0);
    float3 acc = sampleCube(d);
    const float radius = rough * rough * 0.4;
    if (radius < 0.02) {
        return acc;
    }
    const float3 up = abs(d.y) < 0.85 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 t = normalize(cross(up, d));
    const float3 b = cross(d, t);
    acc += sampleCube(normalize(d + t * radius));
    acc += sampleCube(normalize(d - t * radius));
    acc += sampleCube(normalize(d + b * radius));
    acc += sampleCube(normalize(d - b * radius));
    return acc * 0.2;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectTrace(uint3 dtid : SV_DispatchThreadID) {
    if (gPass.lightProfile < 1u || (gFrame.debugMode != 0u && gFrame.debugMode != 7u)) {
        return;
    }
    const uint2 px = dtid.xy;
    if (px.x >= uint(gFrame.extent.x) || px.y >= uint(gFrame.extent.y)) {
        return;
    }
    const uint vis = gVis.Load(int3(px, 0));
    const float depth = gDepth.Load(int3(px, 0));
    if (vis == 0u || depth <= 0.0) {
        return;
    }
    const float4 surf = gSurf0[int2(px)];
    const float4 surfN = gSurf1[int2(px)];
    const float3 P = reconstructWorld(px, depth);
    float3 N = octDecode(surfN.xy);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    }
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    const float rough = surf.w;
    const float metal = saturate(surfN.z);
    const float fresnel = lerp(0.04, 1.0, pow(saturate(1.0 - dot(N, V)), 5.0));
    const float smooth = saturate((0.4 - rough) / 0.4);
    const float weight = lerp(smooth * fresnel, 1.0, metal);
    const float3 dir = reflect(-V, N);
    float3 col = sampleWorld(dir, rough);
    col *= lerp(float3(1.0, 1.0, 1.0), surf.rgb, metal);
    if (gFrame.debugMode == 7u) {
        gHdr[int2(px)] = float4(col, 1.0);
        return;
    }
    const float3 base = gHdr[int2(px)].rgb;
    gHdr[int2(px)] = float4(lerp(base, col, saturate(weight)), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectUp(uint3 dtid : SV_DispatchThreadID) {
    if (gPass.lightProfile != 1u) {
        return;
    }
    if (dtid.x >= uint(gFrame.extent.x) || dtid.y >= uint(gFrame.extent.y)) {
        return;
    }
    const float depth = gDepth.Load(int3(dtid.xy, 0));
    const int2 halfPx = int2(dtid.xy) / 2;
    float4 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 s = halfPx + int2(x, y);
            const float4 tap = gReflect[s];
            const float2 full = float2(s * 2);
            const float sd = gDepth.Load(int3(int2(full), 0));
            const float w = exp(-abs(sd - depth) * 40.0);
            acc += tap * w;
            wsum += w;
        }
    }
    const float4 tap = wsum > 1e-4 ? acc / wsum : 0.0;
    if (gFrame.debugMode == 7u) {
        gHdr[int2(dtid.xy)] = float4(tap.rgb, 1.0);
        return;
    }
    gHdr[int2(dtid.xy)] += float4(tap.rgb * tap.a, 0.0);
}
