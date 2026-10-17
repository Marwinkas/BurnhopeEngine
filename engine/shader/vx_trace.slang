#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "core/atmosphere.slang"
#include "vx_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;
[[vk::binding(61, 0)]] Texture3D<float4> gVoxel[6];

float3 vxSkyAmb(float3 N, float3 albedo, float metal) {
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    const float3 skyDir = normalize(float3(N.x, N.y + 1.15, N.z));
    const float3 skyLit = max(gSkyView.SampleLevel(gClamp, atmSkyUv(skyDir), 0.0).rgb, 0.0);
    const float skyLum = dot(skyLit, float3(0.2126, 0.7152, 0.0722));
    const float3 skyChroma = skyLum > 0.05 ? skyLit / skyLum : float3(0.45, 0.62, 1.0);
    const float wrap = saturate(N.y * 0.4 + 0.6);
    float3 sky = albedo * (1.0 - metal) * skyChroma * (inten * 0.16) * wrap;
    if ((gPass.passMask & 256u) == 0u) {
        sky *= 0.15;
    }
    return sky;
}

bool vxPixel(uint2 px, out float depth) {
    depth = 0.0;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return false;
    }
    if (gVis.Load(int3(px, 0)) == 0u) {
        return false;
    }
    depth = gDepth.Load(int3(px, 0));
    return depth > 0.0;
}

float4 vxSampleLevel(uint cascade, float3 world, float lod) {
    const VxCascade cas = vxLevel(cascade);
    const float3 local = world / cas.voxel - float3(cas.anchorX, cas.anchorY, cas.anchorZ);
    if (any(local < 0.0) || any(local >= float(kVxRes))) {
        return 0.0;
    }
    const float edge = min(
        min(local.x, float(kVxRes) - local.x),
        min(min(local.y, float(kVxRes) - local.y), min(local.z, float(kVxRes) - local.z)));
    const float fade = saturate(edge / 12.0);
    const float3 uv = frac((floor(local) + float3(cas.scrollX, cas.scrollY, cas.scrollZ) + frac(local)) / float(kVxRes));
    const uint img = gPush.flight * 3u + cascade;
    float4 tap = gVoxel[img].SampleLevel(gWrap, uv, clamp(lod, 0.0, 7.0));
    if (tap.a > 1e-3) {
        tap.rgb /= tap.a;
    }
    tap.a *= fade;
    return tap;
}

float4 vxSample(float3 world, float diameter) {
    float4 acc = vxSampleLevel(2u, world, log2(max(diameter / gVx.level2.voxel, 1.0)));
    const float4 mid = vxSampleLevel(1u, world, log2(max(diameter / gVx.level1.voxel, 1.0)));
    acc = lerp(acc, mid, saturate(mid.a));
    const float4 fine = vxSampleLevel(0u, world, log2(max(diameter / gVx.level0.voxel, 1.0)));
    acc = lerp(acc, fine, saturate(fine.a));
    return acc;
}

float3 vxCone(float3 origin, float3 dir, float tanHalf, float maxDist) {
    float3 acc = 0.0;
    float alpha = 0.0;
    float dist = max(gVx.level0.voxel * 2.0, 0.2);
    [loop]
    for (uint i = 0u; i < 11u; ++i) {
        if (alpha > 0.95 || dist > maxDist) {
            break;
        }
        const float diameter = max(2.0 * tanHalf * dist, gVx.level0.voxel);
        const float4 tap = vxSample(origin + dir * dist, diameter);
        const float a = saturate(tap.a);
        acc += (1.0 - alpha) * max(tap.rgb, 0.0) * a;
        alpha += (1.0 - alpha) * a;
        dist += max(diameter, gVx.level0.voxel);
    }
    return acc;
}

float3 vxDir(float3 N, float3 T, float3 B, float3 cone) {
    return normalize(T * cone.x + N * cone.y + B * cone.z);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void vxTrace(uint3 dtid : SV_DispatchThreadID) {
    if ((gPass.passMask & 524288u) == 0u) {
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    float depth = 0.0;
    if (!vxPixel(dtid.xy, depth)) {
        return;
    }
    const uint2 px = dtid.xy;
    const float3 P = reconstructWorld(px, depth);
    float3 N = octDecode(gSurf1[px].xy);
    if (dot(N, N) < 0.5) {
        N = float3(0.0, 1.0, 0.0);
    } else {
        N = normalize(N);
    }
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 up = abs(N.y) < 0.95 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 T = normalize(cross(up, N));
    const float3 B = cross(N, T);
    const float aperture = 0.577;
    const float3 c0 = vxCone(P, N, 0.36, 128.0);
    const float3 c1 = vxCone(P, vxDir(N, T, B, float3(0.866025, 0.5, 0.0)), aperture, 128.0);
    const float3 c2 = vxCone(P, vxDir(N, T, B, float3(0.267617, 0.5, 0.823639)), aperture, 128.0);
    const float3 c3 = vxCone(P, vxDir(N, T, B, float3(-0.700629, 0.5, 0.509037)), aperture, 128.0);
    const float3 c4 = vxCone(P, vxDir(N, T, B, float3(-0.700629, 0.5, -0.509037)), aperture, 128.0);
    const float3 c5 = vxCone(P, vxDir(N, T, B, float3(0.267617, 0.5, -0.823639)), aperture, 128.0);
    const float3 incoming = (c0 + c1 + c2 + c3 + c4 + c5) * (1.0 / 6.0);
    const float3 albedo = max(gSurf0[px].rgb, 0.0);
    const float rough = saturate(gSurf0[px].a);
    const float metal = saturate(gSurf1[px].z);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metal);
    const float3 R = reflect(-V, N);
    const float specAperture = lerp(0.03, 0.75, rough);
    const float3 spec = vxCone(P, R, specAperture, 48.0);
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    const float gain = gShade.pad1 > 1e-4 ? gShade.pad1 : 1.0;
    const float3 tint = albedo * lerp(1.0, 0.04, metal);
    float3 gi = incoming * tint * (inten * 0.35) * gain;
    gi += spec * f0 * lerp(1.0, 0.15, rough) * (inten * 0.2) * gain;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(max(incoming, 0.0) * 0.8 + vxSkyAmb(N, albedo, metal), 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += max(gi, 0.0);
    gHdr[px] = hdr;
}
