// One draw. Glyph coverage is a FreeType SDF (0.5 = edge). Swapchain is UNORM.

[[vk::binding(0, 0)]]
ByteAddressBuffer gUi;
[[vk::binding(1, 0)]]
Texture2D<float> gSdf;
[[vk::binding(2, 0)]]
Texture2D<float4> gPhoto[8];
[[vk::binding(0, 1)]]
SamplerState gSdfSamp;

static const uint kPrimBase = 64;
static const uint kFontBase = 64 + 2048 * 64;

struct VsOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;
    float2 origin : TEXCOORD1;
    float4 color : TEXCOORD2;
    nointerpolation float radius : TEXCOORD3;
    nointerpolation float2 size : TEXCOORD4;
    nointerpolation float4 clip : TEXCOORD5;
    nointerpolation uint flags : TEXCOORD6;
    float2 uv : TEXCOORD7;
    nointerpolation uint slot : TEXCOORD8;
    nointerpolation float spread : TEXCOORD9;
};

static const float2 kCorner[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0),
};

[shader("vertex")]
VsOut uiVs(uint vid : SV_VertexID) {
    VsOut o;
    const uint prim = vid / 6;
    const uint base = kPrimBase + prim * 64;
    const float2 pos = asfloat(gUi.Load2(base));
    const float2 size = asfloat(gUi.Load2(base + 8));
    const float4 color = asfloat(gUi.Load4(base + 16));
    const uint2 packed = gUi.Load2(base + 32);
    const float radius = asfloat(gUi.Load(base + 48));
    const float4 extra = asfloat(gUi.Load4(base + 48));
    const float clipH = asfloat(gUi.Load(base + 40));
    const float2 extent = asfloat(gUi.Load2(0));
    const float2 corner = kCorner[vid % 6];
    const float2 xy = pos + corner * size;
    const float2 ndc = float2(xy.x / extent.x * 2.0 - 1.0, xy.y / extent.y * 2.0 - 1.0);
    o.pos = float4(ndc, 0.0, 1.0);
    o.local = corner * size;
    o.origin = pos;
    o.color = color;
    o.radius = radius;
    o.size = size;
    o.clip = float4(extra.y, extra.z, extra.w, clipH);
    o.flags = packed.y;
    o.uv = float2(0.0, 0.0);
    o.slot = 0;
    o.spread = asfloat(gUi.Load(base + 44));
    if ((packed.y & 32u) != 0) {
        o.uv = lerp(extra.xy, extra.zw, corner);
        o.radius = 6.0;
        o.slot = packed.x;
        if ((packed.y & 2u) != 0) {
            o.clip = color;
        }
    } else if ((packed.y & 4u) != 0 || (packed.y & 16u) != 0) {
        o.uv = float2((float)packed.x, 0.0);
    } else if ((packed.y & 1u) != 0) {
        uint index = packed.x;
        if (index >= 192u) {
            index = 31u;
        }
        const float4 uvb = asfloat(gUi.Load4(kFontBase + index * 16));
        o.uv = lerp(uvb.xy, uvb.zw, corner);
    }
    return o;
}

float4 samplePhoto(uint slot, float2 uv) {
    if (slot == 1u) return gPhoto[1].Sample(gSdfSamp, uv);
    if (slot == 2u) return gPhoto[2].Sample(gSdfSamp, uv);
    if (slot == 3u) return gPhoto[3].Sample(gSdfSamp, uv);
    if (slot == 4u) return gPhoto[4].Sample(gSdfSamp, uv);
    if (slot == 5u) return gPhoto[5].Sample(gSdfSamp, uv);
    if (slot == 6u) return gPhoto[6].Sample(gSdfSamp, uv);
    if (slot == 7u) return gPhoto[7].Sample(gSdfSamp, uv);
    return gPhoto[0].Sample(gSdfSamp, uv);
}

float3 hsv2rgb(float h, float s, float v) {
    float3 p = abs(frac(h + float3(1.0, 0.6666667, 0.3333333)) * 6.0 - 3.0);
    return v * lerp(float3(1.0, 1.0, 1.0), saturate(p - 1.0), s);
}

[shader("fragment")]
float4 uiFs(VsOut i) : SV_Target0 {
    float cov = 1.0;
    float3 rgb = i.color.rgb;
    float alpha = i.color.a;
    if ((i.flags & 32u) != 0) {
        const float4 tex = samplePhoto(min(i.slot, 7u), saturate(i.uv));
        rgb = tex.rgb;
        alpha = tex.a;
        const float r = min(i.radius, min(i.size.x, i.size.y) * 0.5);
        const float2 halfSize = i.size * 0.5;
        const float2 q = abs(i.local - halfSize) - (halfSize - r);
        const float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        const float aa = max(fwidth(d), 0.0001);
        cov = saturate(0.5 - d / aa);
    } else if ((i.flags & 8u) != 0) {
        const float2 p = i.local / max(i.size, float2(1.0, 1.0)) * 2.0 - 1.0;
        const float rad = length(p);
        const float hue = atan2(p.y, p.x) * 0.15915494 + 0.5;
        rgb = hsv2rgb(hue, saturate(rad), 1.0);
        const float w = max(fwidth(rad), 0.002);
        cov = saturate((1.02 - rad) / w);
        alpha = 1.0;
    } else if ((i.flags & 16u) != 0) {
        const float u = saturate(i.local.x / max(i.size.x, 1.0));
        const float v = saturate(1.0 - i.local.y / max(i.size.y, 1.0));
        const uint mode = (uint)i.uv.x;
        if (mode == 1u) {
            rgb = i.color.rgb * v;
        } else if (mode == 2u) {
            const float2 cell = floor(i.local / 6.0);
            const float check = fmod(cell.x + cell.y, 2.0) < 0.5 ? 0.78 : 0.42;
            rgb = lerp(float3(check, check, check), i.color.rgb, u);
        } else if (mode == 3u) {
            rgb = hsv2rgb(u, 1.0, 1.0);
        } else if (mode == 4u) {
            rgb = lerp(float3(1.0, 1.0, 1.0), i.color.rgb, u);
        } else if (mode == 6u) {
            const float2 cell = floor(i.local / 8.0);
            const float check = fmod(cell.x + cell.y, 2.0) < 0.5 ? 0.78 : 0.42;
            rgb = float3(check, check, check);
        } else {
            rgb = i.color.rgb * u;
        }
        alpha = 1.0;
    } else if ((i.flags & 4u) != 0) {
        const float2 p = i.local / max(i.size, float2(1.0, 1.0)) * 2.0 - 1.0;
        const uint icon = (uint)i.uv.x;
        if (icon == 1u) {
            cov = abs(p.y) < 0.16 && abs(p.x) < 0.62 ? 1.0 : 0.0;
        } else if (icon == 2u) {
            const float m = max(abs(p.x), abs(p.y));
            cov = m < 0.62 && m > 0.34 ? 1.0 : 0.0;
        } else if (icon == 3u) {
            cov = abs(abs(p.x) - abs(p.y)) < 0.16 && max(abs(p.x), abs(p.y)) < 0.7 ? 1.0 : 0.0;
        } else if (icon == 4u) {
            cov = (abs(p.y + 0.15) < 0.45 && abs(p.x) < 0.55) || (p.y < -0.15 && abs(p.x) < 0.35 && p.y > -0.55) ? 1.0 : 0.0;
        } else if (icon == 5u) {
            const float ang = atan2(p.y, p.x);
            const float rad = length(p);
            const float tooth = 0.46 + 0.16 * step(0.0, sin(ang * 6.0));
            cov = (rad < tooth && rad > 0.26) || rad < 0.14 ? 1.0 : 0.0;
        } else if (icon == 6u) {
            cov = abs(p.x) < 0.62 && (abs(p.y) < 0.08 || abs(p.y - 0.36) < 0.08 || abs(p.y + 0.36) < 0.08) ? 1.0 : 0.0;
        } else if (icon == 7u) {
            const float2 tip = p - float2(-0.22, 0.22);
            const float bowl = length(tip);
            const bool stem = abs(p.x + p.y) < 0.14 && p.x > -0.05 && p.y < 0.05;
            cov = (bowl < 0.42 && bowl > 0.22) || bowl < 0.12 || stem ? 1.0 : 0.0;
        }
    } else if ((i.flags & 1u) != 0) {
        const float sd = gSdf.SampleLevel(gSdfSamp, i.uv, 0.0).r;
        const float w = clamp(fwidth(sd), 0.015, 0.06);
        cov = smoothstep(0.5 - w, 0.5 + w, sd);
    } else {
        const float r = min(i.radius, min(i.size.x, i.size.y) * 0.5);
        const float2 halfSize = i.size * 0.5;
        const float2 q = abs(i.local - halfSize) - (halfSize - r);
        const float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        const float aa = max(fwidth(d), 0.0001);
        cov = saturate(0.5 - d / aa);
        if ((i.flags & 128u) != 0) {
            const float spread = max(i.spread, 1.0);
            const float distOut = d + spread;
            const float fall = saturate(1.0 - max(distOut, 0.0) / spread);
            cov *= fall;
        } else if ((i.flags & 64u) != 0) {
            const float inner = d + max(i.spread, 0.5);
            const float hole = saturate(0.5 - inner / aa);
            cov = saturate(cov - hole);
        }
    }
    if ((i.flags & 2u) != 0) {
        const float2 pix = i.origin + i.local;
        if (pix.x < i.clip.x || pix.y < i.clip.y || pix.x >= i.clip.x + i.clip.z || pix.y >= i.clip.y + i.clip.w) {
            cov = 0.0;
        }
    }
    float4 c = float4(rgb, alpha);
    c.a *= cov;
    return c;
}
