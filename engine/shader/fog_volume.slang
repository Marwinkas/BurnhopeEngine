// Фроксели 128×72×48, шаг по глубине логарифмический. Старый высотный марш сюда не входит.

#include "frame.slang"
#include "camera.slang"

static const uint kFogW = 128;
static const uint kFogH = 72;
static const uint kFogD = 48;

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(90, 0)]] RWTexture3D<float4> gScatter;
[[vk::binding(91, 0)]] RWTexture3D<float4> gIntegrated;

float miePhase(float cosT) {
    const float g = 0.8;
    const float g2 = g * g;
    return (1.0 / (4.0 * 3.14159265)) * (1.0 - g2) / pow(max(1.0 + g2 - 2.0 * g * cosT, 1e-3), 1.5);
}

float fogNoise(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    const float a = frac(sin(dot(i, float2(127.1, 311.7))) * 43758.5453);
    const float b = frac(sin(dot(i + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
    const float c = frac(sin(dot(i + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    const float d = frac(sin(dot(i + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float sliceDist(float z) {
    const float nearD = 0.4;
    const float farD = 140.0;
    return nearD * pow(farD / nearD, saturate(z));
}

[shader("compute")]
[numthreads(8, 8, 4)]
void fogScatter(uint3 id : SV_DispatchThreadID) {
    if (id.x >= kFogW || id.y >= kFogH || id.z >= kFogD) {
        return;
    }
    const float2 uv = (float2(id.xy) + 0.5) / float2(kFogW, kFogH);
    const float3 cam = gFrame.cameraPos.xyz;
    const float3 farP = reconstructWorld(uv, 0.0);
    float3 dir = farP - cam;
    const float len = max(length(dir), 1e-3);
    dir /= len;
    const float t0 = sliceDist((float(id.z) + 0.0) / float(kFogD));
    const float t1 = sliceDist((float(id.z) + 1.0) / float(kFogD));
    const float dt = max(t1 - t0, 0.05);
    const float3 pos = cam + dir * t0;
    const float density = max(gShade.fogDensity, 0.0);
    const float falloff = max(gRcParams.fogFalloff, 0.0);
    const float y = pos.y - gShade.fogHeight;
    const float profile = falloff < 1e-4 ? 1.0 : exp(-max(y, 0.0) * falloff);
    const float start = max(gRcParams.fogStart, 0.0);
    const float cover = t1 <= start ? 0.0 : saturate((t1 - max(t0, start)) / dt);
    float haze = density * profile * cover;
    haze *= lerp(0.82, 1.18, fogNoise(pos.xz * 0.07 + pos.y * 0.03));
    const float rain = saturate(gFrame.viewPad.w);
    haze += density * 0.45 * exp(-max(y, 0.0) * max(falloff, 0.4) * 6.0) * rain * cover;
    haze *= saturate(gRcParams.fogOpacity);
    const float3 tint = max(float3(gRcParams.fogR, gRcParams.fogG, gRcParams.fogB), 0.0);
    const float lum = max(gRcParams.fogLum, 0.0);
    const float3 sunDir = normalize(gSun.sunDirIntensity.xyz + float3(0.0, 1e-4, 0.0));
    float shaft = 0.0;
    if (gShade.fogScatter > 0.5 && t0 >= gRcParams.fogInStart) {
        shaft = pow(saturate(dot(dir, sunDir)), gShade.fogScatter);
    }
    const float3 sun = gSun.sunColor.rgb * gSun.sunDirIntensity.w;
    const float3 inscatter = tint * lum * haze * dt * (0.05 + shaft * 1.6) + sun * shaft * haze * dt * 0.4;
    gScatter[id] = float4(max(inscatter, 0.0), haze * dt);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void fogIntegrate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= kFogW || id.y >= kFogH) {
        return;
    }
    float3 accum = 0.0;
    float trans = 1.0;
    [loop]
    for (uint z = 0u; z < kFogD; ++z) {
        const float4 s = gScatter[uint3(id.xy, z)];
        accum += trans * s.rgb;
        trans *= exp(-s.a);
        gIntegrated[uint3(id.xy, z)] = float4(accum, trans);
    }
}

float4 fogLoad(int2 xy, uint z) {
    const int2 p = clamp(xy, int2(0, 0), int2(kFogW - 1, kFogH - 1));
    return gIntegrated[uint3(p, min(z, kFogD - 1u))];
}

[shader("compute")]
[numthreads(8, 8, 1)]
void fogApply(uint3 id : SV_DispatchThreadID) {
    if (id.x >= gFrame.extent.x || id.y >= gFrame.extent.y || gFrame.debugMode != 0u) {
        return;
    }
    const float depth = gDepth.Load(int3(id.xy, 0));
    const float2 uv = (float2(id.xy) + 0.5) * gFrame.invExtent;
    const float3 cam = gFrame.cameraPos.xyz;
    float4 hdr = gHdr[id.xy];
    if (depth > 0.001) {
        const float3 world = reconstructWorld(uv, depth);
        const float dist = length(world - cam);
        const float nearD = 0.4;
        const float farD = 140.0;
        const float u = log(clamp(dist, nearD, farD) / nearD) / log(farD / nearD);
        const float zf = saturate(u) * float(kFogD - 1u);
        const uint z0 = uint(zf);
        const float fz = zf - float(z0);
        const float2 grid = uv * float2(kFogW - 1, kFogH - 1);
        const int2 g0 = int2(grid);
        const float2 fg = grid - float2(g0);
        const float4 a = lerp(fogLoad(g0, z0), fogLoad(g0 + int2(1, 0), z0), fg.x);
        const float4 b = lerp(fogLoad(g0 + int2(0, 1), z0), fogLoad(g0 + int2(1, 1), z0), fg.x);
        const float4 c = lerp(fogLoad(g0, z0 + 1u), fogLoad(g0 + int2(1, 0), z0 + 1u), fg.x);
        const float4 d = lerp(fogLoad(g0 + int2(0, 1), z0 + 1u), fogLoad(g0 + int2(1, 1), z0 + 1u), fg.x);
        const float4 fog = lerp(lerp(a, b, fg.y), lerp(c, d, fg.y), fz);
        hdr.rgb = hdr.rgb * fog.a + fog.rgb;
    }
    gHdr[id.xy] = hdr;
}
