// HDR → swapchain. Filament tone mappers, grade, bloom, DoF, flares, FXAA.
// Grade lives in the 64-byte PostPack at binding 56. BloomParams stays binding 57.

#include "frame.slang"
#include "core/math.slang"
#include "core/grade.slang"
#include "camera.slang"

[[vk::binding(6, 0)]] Texture2D<float4> gHdr;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(33, 0)]] Texture2D<float4> gBloom;
[[vk::binding(1, 1)]] SamplerState gLinear;

struct GradeParams {
    float exposure;
    float temperature;
    float tint;
    float contrast;
    float saturation;
    float night;
    float shadows;
    float midtones;
    float highlights;
    float slope;
    float cdlOffset;
    float power;
    float dofFocus;
    float dofAmount;
    float flare;
    float flags;
};

[[vk::binding(56, 0)]] ConstantBuffer<GradeParams> gGrade;

[[vk::binding(60, 0)]] ConstantBuffer<GradeBlock> gBlock;

struct VertOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void tonemapMesh(
    uint gtid : SV_GroupThreadID,
    out vertices VertOut verts[3],
    out indices uint3 prims[1])
{
    if (gtid == 0) {
        SetMeshOutputCounts(3, 1);
        prims[0] = uint3(0, 1, 2);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < 3) {
        const float2 ndc = float2((gtid << 1) & 2, gtid & 2) * 2.0 - 1.0;
        verts[gtid].pos = float4(ndc, 0.0, 1.0);
        verts[gtid].uv = float2(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5);
    }
}

float3 mul3(float3x3 m, float3 v) {
    return mul(m, v);
}

float3 agxContrast(float3 x) {
    const float3 x2 = x * x;
    const float3 x4 = x2 * x2;
    const float3 x6 = x4 * x2;
    return -17.86 * x6 * x + 78.01 * x6 - 126.7 * x4 * x + 92.06 * x4
        - 28.72 * x2 * x + 4.361 * x2 - 0.1718 * x + 0.002857;
}

float3 agx(float3 val) {
    const float3x3 inset = float3x3(
        0.856627153, 0.095121241, 0.048251606,
        0.137318973, 0.761241991, 0.101439036,
        0.111898213, 0.076799419, 0.811302368);
    const float3x3 outset = float3x3(
        1.127100582, -0.110606643, -0.016493939,
        -0.141329763, 1.157823702, -0.016493939,
        -0.141329763, -0.110606643, 1.251936407);
    val = mul3(inset, max(val, 0.0));
    val = log2(max(val, 1e-10));
    val = saturate((val - (-12.47393)) / (4.026069 - (-12.47393)));
    val = agxContrast(val);
    val = max(mul3(outset, val), 0.0);
    return pow(val, 2.2);
}

float acesSat(float3 rgb) {
    const float lo = min(rgb.r, min(rgb.g, rgb.b));
    const float hi = max(rgb.r, max(rgb.g, rgb.b));
    return (max(hi, 1e-5) - max(lo, 1e-5)) / max(hi, 1e-2);
}

float acesYc(float3 rgb) {
    const float chroma = sqrt(max(rgb.b * (rgb.b - rgb.g) + rgb.g * (rgb.g - rgb.r) + rgb.r * (rgb.r - rgb.b), 0.0));
    return (rgb.r + rgb.g + rgb.b + 1.75 * chroma) / 3.0;
}

float acesHue(float3 rgb) {
    if (rgb.r == rgb.g && rgb.g == rgb.b) {
        return 0.0;
    }
    float hue = degrees(atan2(sqrt(3.0) * (rgb.g - rgb.b), 2.0 * rgb.r - rgb.g - rgb.b));
    return hue < 0.0 ? hue + 360.0 : hue;
}

float3 aces(float3 color) {
    const float3x3 toAp0 = float3x3(
        0.439701000, 0.382978000, 0.177335000,
        0.089792300, 0.813423000, 0.096761600,
        0.017544000, 0.111544000, 0.870704000);
    const float3x3 ap0ToAp1 = float3x3(
        1.451439316, -0.236510747, -0.214928569,
        -0.076553773, 1.176229700, -0.099675926,
        0.008316148, -0.006032450, 0.997716301);
    const float3x3 ap1ToSrgb = float3x3(
        1.705050000, -0.621790000, -0.083260000,
        -0.130260000, 1.140800000, -0.010550000,
        -0.024000000, -0.128970000, 1.152970000);
    const float3 lumAp1 = float3(0.272229, 0.674082, 0.0536895);
    float3 ap0 = mul3(toAp0, max(color, 0.0));
    const float sat = acesSat(ap0);
    const float yc = acesYc(ap0);
    const float t = max(1.0 - abs(((sat - 0.4) / 0.2) * 0.5), 0.0);
    const float shaper = (1.0 + sign(sat - 0.4) * (1.0 - t * t)) * 0.5;
    float glow = 0.05 * shaper;
    if (yc > 2.0 / 3.0 * 0.08 && yc < 2.0 * 0.08) {
        glow *= (0.08 / max(yc, 1e-4) - 0.5);
    } else if (yc >= 2.0 * 0.08) {
        glow = 0.0;
    }
    ap0 *= 1.0 + glow;
    const float hueW = pow(smoothstep(0.0, 1.0, 1.0 - abs(2.0 * acesHue(ap0) / 135.0)), 2.0);
    ap0.r += hueW * sat * (0.03 - ap0.r) * (1.0 - 0.82);
    float3 ap1 = clamp(mul3(ap0ToAp1, ap0), 0.0, 65504.0);
    ap1 = lerp(dot(ap1, lumAp1).xxx, ap1, 0.96);
    const float3 rgbPost = (ap1 * (2.785085 * ap1 + 0.107772)) / (ap1 * (2.936045 * ap1 + 0.887122) + 0.806889);
    const float y = max(dot(rgbPost, lumAp1), 1e-5);
    float3 linearCV = rgbPost * (pow(y, 0.9811) / y);
    linearCV = lerp(dot(linearCV, lumAp1).xxx, linearCV, 0.93);
    return max(mul3(ap1ToSrgb, linearCV), 0.0);
}

float3 filmic(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 pbrNeutral(float3 color) {
    const float startCompression = 0.76;
    const float x = min(color.r, min(color.g, color.b));
    const float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    const float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }
    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / max(peak, 1e-4);
    const float g = 1.0 - 1.0 / (0.15 * (peak - newPeak) + 1.0);
    return lerp(color, newPeak.xxx, g);
}

float gt7Curve(float x) {
    if (x < 0.0) {
        return 0.0;
    }
    const float peak = 2.5;
    const float alpha = 0.25;
    const float mid = 0.538;
    const float linearSection = 0.444;
    const float toe = 1.280;
    const float k = (linearSection - 1.0) / (alpha - 1.0);
    const float ka = peak * linearSection + peak * k;
    const float kb = -peak * k * exp(linearSection / k);
    const float kc = -1.0 / (k * peak);
    if (x < linearSection * peak) {
        const float weightLinear = smoothstep(0.0, mid, x);
        const float toeMapped = mid * pow(max(x / mid, 0.0), toe);
        return (1.0 - weightLinear) * toeMapped + weightLinear * x;
    }
    return ka + kb * exp(x * kc);
}

float3 gt7(float3 color) {
    const float3 rec = srgbToRec2020(color);
    const float3 skewed = float3(gt7Curve(rec.r), gt7Curve(rec.g), gt7Curve(rec.b));
    const float3 ucs = rec2020ToICtCp(rec * 100.0);
    const float3 skewedUcs = rec2020ToICtCp(skewed * 100.0);
    const float targetUcs = rec2020ToICtCp(float3(250.0, 250.0, 250.0)).x;
    const float chroma = 1.0 - smoothstep(0.98, 1.16, ucs.x / max(targetUcs, 1e-4));
    const float3 scaled = iCtCpToRec2020(float3(skewedUcs.x, ucs.y * chroma, ucs.z * chroma)) / 100.0;
    return rec2020ToSrgb(0.4 * min(lerp(skewed, scaled, 0.6), 2.5));
}

float3 genericMap(float3 x) {
    return genericTone(x, gBlock.generic.x, gBlock.generic.y, gBlock.generic.z, gBlock.generic.w);
}

float3 toneMap(float3 hdr) {
    if (gTone.tonemapper == 1u) {
        return aces(hdr);
    }
    if (gTone.tonemapper == 2u) {
        return filmic(hdr);
    }
    if (gTone.tonemapper == 3u) {
        return max(hdr, 0.0);
    }
    if (gTone.tonemapper == 4u) {
        return pbrNeutral(max(hdr, 0.0));
    }
    if (gTone.tonemapper == 5u) {
        return gt7(max(hdr, 0.0));
    }
    if (gTone.tonemapper == 6u) {
        return genericMap(hdr);
    }
    if (gTone.tonemapper == 7u) {
        return hdr / (1.0 + max(hdr, 0.0));
    }
    return agx(hdr);
}

float3 channelMixer(float3 v) {
    return float3(dot(v, gBlock.mixR.xyz), dot(v, gBlock.mixG.xyz), dot(v, gBlock.mixB.xyz));
}

float3 tonalRanges(float3 v) {
    const float y = dot(v, float3(0.2126, 0.7152, 0.0722));
    const float s = 1.0 - smoothstep(gBlock.ranges.x, gBlock.ranges.y, y);
    const float h = smoothstep(gBlock.ranges.z, gBlock.ranges.w, y);
    const float m = 1.0 - s - h;
    return v * s * gBlock.shadows.xyz + v * m * gBlock.midtones.xyz + v * h * gBlock.highlights.xyz;
}

float3 cdl(float3 v) {
    v = v * gBlock.slope.xyz + gBlock.offset.xyz;
    const float3 pv = pow(max(v, 0.0), gBlock.power.xyz);
    return float3(v.r <= 0.0 ? v.r : pv.r, v.g <= 0.0 ? v.g : pv.g, v.b <= 0.0 ? v.b : pv.b);
}

float3 gradeColor(float3 v) {
    v = nightAdapt(v, gGrade.night);
    v = whiteBalance(v, gGrade.temperature, gGrade.tint);
    v = max(v, 0.0);
    v = channelMixer(v);
    v = tonalRanges(v);
    v = linearToLogC(max(v, 0.0));
    v = cdl(v);
    v = v * gGrade.contrast + 0.4135884 * (1.0 - gGrade.contrast);
    v = max(logCToLinear(v), 0.0);
    v = vibranceGrade(v, gBlock.curves.w);
    const float y = luma(v);
    v = y + gGrade.saturation * (v - y);
    v = max(v, 0.0);
    return curvesGrade(v, gBlock.curves.x, gBlock.curves.y, gBlock.curves.z);
}

float3 luminanceScale(float3 x) {
    const float3 w = float3(0.2126, 0.7152, 0.0722);
    const float luminanceIn = max(dot(x, w), 1e-5);
    const float luminanceOut = toneMap(luminanceIn.xxx).g;
    const float peak = max(x.r, max(x.g, x.b));
    const float3 chromaRatio = max(x / max(peak, 1e-5), 0.0);
    const float chromaLum = max(dot(chromaRatio, w), 1e-5);
    const float3 reserves = 1.0 - chromaRatio;
    const float reserveLum = max(dot(reserves, w), 1e-5);
    const float difference = max(luminanceOut - chromaLum, 0.0);
    const float chromaScale = (luminanceOut - difference) / chromaLum;
    return chromaScale * chromaRatio + (difference / reserveLum) * reserves;
}

float3 finishMap(float3 hdr) {
    if ((uint(gGrade.flags + 0.5) & 2u) != 0u) {
        return luminanceScale(hdr);
    }
    return toneMap(hdr);
}

float sceneDist(float2 uv) {
    const int2 px = int2(uv * float2(gFrame.extent.xy));
    const float depth = gDepth.Load(int3(clamp(px, int2(0, 0), int2(gFrame.extent.xy) - 1), 0));
    if (depth < 1e-4) {
        return 200.0;
    }
    const float3 P = reconstructWorld(uv, depth);
    return length(P - gFrame.cameraPos.xyz);
}

float circleCoc(float2 uv, float2 screen) {
    const float dist = max(sceneDist(uv), 0.05);
    const float focus = max(gGrade.dofFocus, 0.05);
    const float K = gGrade.dofAmount * screen.y * 0.035;
    float coc = 0.5 * K * (1.0 - focus / dist);
    coc = clamp(coc, -32.0, 32.0);
    return abs(coc) < 1.0 ? 0.0 : coc;
}

float cocWeight(float coc) {
    return (32.0 * 32.0) / max(coc * coc, 2.0);
}

float3 bokehGather(float2 uv, float3 center, float2 invPx) {
    const uint2 screen = gFrame.padExt.x > 0u ? gFrame.padExt : gFrame.extent;
    const float centerCoc = circleCoc(uv, float2(screen));
    if (abs(centerCoc) < 1.0) {
        return center;
    }
    const int rings = clamp(int(ceil(abs(centerCoc) / 8.0)), 1, 4);
    float3 color = center * cocWeight(centerCoc);
    float weight = cocWeight(centerCoc);
    [loop]
    for (int ring = 1; ring <= rings; ++ring) {
        const float count = 8.0 * float(ring);
        const float radius = abs(centerCoc) * float(ring) / (float(rings) - 0.5);
        const float inc = 6.2831853 / count;
        float3 ringColor = 0.0;
        float ringWeight = 0.0;
        [loop]
        for (int s = 0; s < int(count); ++s) {
            const float ang = inc * float(s) + float(ring % 2) * 0.5 * inc;
            const float2 tap = uv + float2(cos(ang), sin(ang)) * radius * invPx;
            const float coc = abs(circleCoc(tap, float2(screen)));
            const float hit = saturate((coc - radius) + 0.5);
            const float w = hit * cocWeight(max(coc, 1.0));
            ringColor += gHdr.SampleLevel(gLinear, tap, 0.0).rgb * w;
            ringWeight += w;
        }
        const float occluded = saturate(abs(centerCoc) - radius / max(abs(centerCoc), 1.0));
        const float keep = 1.0 - saturate(ringWeight) * occluded * 0.15;
        color = color * keep + ringColor;
        weight = weight * keep + ringWeight;
    }
    return color / max(weight, 1e-4);
}

float flareRing(float x, float c, float r) {
    const float t = min(abs(x - c) / max(r, 1e-4), 1.0);
    const float s = t * t * (3.0 - 2.0 * t);
    return 1.0 - s;
}

float3 flareSample(float2 uv) {
    const float2 offset = normalize(0.5001 - uv) * 0.005;
    return float3(
        gBloom.SampleLevel(gLinear, uv + offset, 0.0).r,
        gBloom.SampleLevel(gLinear, uv, 0.0).g,
        gBloom.SampleLevel(gLinear, uv - offset, 0.0).b);
}

float3 lensFlare(float2 uv) {
    const float threshold = max(gTone.threshold, 0.05);
    float3 ghosts = 0.0;
    const float2 flipped = 1.0 - uv;
    const float2 v = (0.5 - flipped) * 0.6;
    [unroll]
    for (int i = 0; i < 4; ++i) {
        const float2 suv = frac(flipped + v * float(i));
        ghosts += clamp(flareSample(suv) - threshold, 0.0, 1.0);
    }
    const float aspect = float(gFrame.extent.x) / max(float(gFrame.extent.y), 1.0);
    float2 dir = float2(0.5001, 0.5001) - uv;
    dir.x *= aspect;
    dir = normalize(dir);
    dir.x /= aspect;
    const float3 halo = clamp(flareSample(uv + dir * 0.4) - threshold, 0.0, 1.0);
    const float2 uvWeight = (uv - float2(0.5, 0.0)) * float2(aspect, 1.0) + float2(0.5, 0.0);
    return ghosts + halo * flareRing(distance(uvWeight, float2(0.5, 0.5)), 0.4, 0.1);
}

float3 sampleHdr(float2 uv, float2 invPx) {
    const float2 d = uv - 0.5;
    const float2 shift = d * gTone.ca * invPx;
    float3 hdr;
    hdr.r = gHdr.SampleLevel(gLinear, uv + shift, 0.0).r;
    hdr.g = gHdr.SampleLevel(gLinear, uv, 0.0).g;
    hdr.b = gHdr.SampleLevel(gLinear, uv - shift, 0.0).b;
    if (gTone.cas > 0.001) {
        const float3 n0 = gHdr.SampleLevel(gLinear, uv + float2(invPx.x, 0.0), 0.0).rgb;
        const float3 n1 = gHdr.SampleLevel(gLinear, uv - float2(invPx.x, 0.0), 0.0).rgb;
        const float3 n2 = gHdr.SampleLevel(gLinear, uv + float2(0.0, invPx.y), 0.0).rgb;
        const float3 n3 = gHdr.SampleLevel(gLinear, uv - float2(0.0, invPx.y), 0.0).rgb;
        hdr += (hdr - (n0 + n1 + n2 + n3) * 0.25) * gTone.cas;
    }
    if (gGrade.dofAmount > 0.001) {
        hdr = bokehGather(uv, hdr, invPx);
    }
    if (gTone.bloomMix > 0.001 && (gPass.passMask & 32u) != 0u) {
        const uint2 screen = gFrame.padExt.x > 0u ? gFrame.padExt : gFrame.extent;
        const uint h0 = max(screen.y >> 1, 1u);
        const uint h1 = max(screen.y >> 2, 1u);
        const uint h2 = max(screen.y >> 3, 1u);
        const uint h3 = max(screen.y >> 4, 1u);
        const float span = float(h0 + h1 + h2 + h3);
        const float2 uvb = float2(uv.x, uv.y * (float(h0) / span));
        hdr += gBloom.SampleLevel(gLinear, uvb, 0.0).rgb * gTone.bloomMix;
    }
    if (gGrade.flare > 0.001) {
        hdr += lensFlare(uv) * gGrade.flare;
    }
    return hdr;
}

float3 display(float2 uv, float2 invPx, uint2 screen) {
    float3 hdr = sampleHdr(uv, invPx);
    hdr *= max(gTone.f4, 0.0) * exp2(gGrade.exposure);
    hdr = gradeColor(hdr);
    float3 color = finishMap(hdr);
    if ((uint(gGrade.flags + 0.5) & 4u) != 0u) {
        color = gamutClip(color);
    }
    const float lum = luma(color);
    if (gTone.grain > 0.001) {
        const float n = frac(sin(dot(uv * float2(screen) + gFrame.cameraPos.xy, float2(12.9898, 78.233))) * 43758.5453);
        color += (n - 0.5) * gTone.grain * saturate(1.15 - lum);
    }
    color *= saturate(1.15 - dot(uv - 0.5, uv - 0.5) * gTone.vignette);
    if (gTone.f5 > 0.5) {
        color = pow(max(color, 0.0), 1.0 / 2.2);
    }
    return saturate(color);
}

float3 fxaa(float2 uv, float2 invPx, uint2 screen) {
    const float2 corner = invPx;
    const float lumaNw = luma(display(uv + float2(-corner.x, -corner.y), invPx, screen));
    const float lumaSw = luma(display(uv + float2(-corner.x, corner.y), invPx, screen));
    const float lumaNe = luma(display(uv + float2(corner.x, -corner.y), invPx, screen));
    const float lumaSe = luma(display(uv + float2(corner.x, corner.y), invPx, screen));
    const float3 center = display(uv, invPx, screen);
    const float lumaM = luma(center);
    const float lumaMax = max(max(lumaNw, lumaSw), max(lumaNe, lumaSe));
    const float lumaMin = min(min(lumaNw, lumaSw), min(lumaNe, lumaSe));
    const float range = max(lumaMax, lumaM) - min(lumaMin, lumaM);
    if (range < max(0.05, lumaMax * 0.125)) {
        return center;
    }
    const float dirSwMinusNe = lumaSw - lumaNe;
    const float dirSeMinusNw = lumaSe - lumaNw;
    float2 dir = float2(dirSwMinusNe + dirSeMinusNw, dirSwMinusNe - dirSeMinusNw);
    const float dirLength = length(dir);
    if (dirLength < 1e-4) {
        return center;
    }
    dir /= dirLength;
    const float3 n1 = display(uv - dir * invPx, invPx, screen);
    const float3 p1 = display(uv + dir * invPx, invPx, screen);
    const float dirAbs = max(abs(dir.x), abs(dir.y)) * 8.0 * 0.015;
    const float2 dir2 = dir * min(range / max(dirAbs, 1e-4), 3.0);
    const float3 n2 = display(uv - dir2 * invPx * 2.0, invPx, screen);
    const float3 p2 = display(uv + dir2 * invPx * 2.0, invPx, screen);
    const float3 pair = n1 + p1;
    float3 blended = ((n2 + p2) * 0.25) + pair * 0.25;
    if (luma(blended) < lumaMin || luma(blended) > lumaMax) {
        blended = pair * 0.5;
    }
    return saturate(lerp(blended, center, 0.25));
}

[shader("fragment")]
float4 tonemapFs(VertOut vin) : SV_Target0 {
    const uint2 screen = gFrame.padExt.x > 0u ? gFrame.padExt : gFrame.extent;
    const float2 invPx = 1.0 / float2(screen);
    float3 color;
    if ((uint(gGrade.flags + 0.5) & 1u) != 0u) {
        color = fxaa(vin.uv, invPx, screen);
    } else {
        color = display(vin.uv, invPx, screen);
    }
    return float4(color, 1.0);
}
