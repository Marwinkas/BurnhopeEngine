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
static const uint kFontBase = 64 + 2048 * 80;

struct VsOut {
    float4 pos : SV_Position;
    float2 local : TEXCOORD0;
    float2 origin : TEXCOORD1;
    float4 color : TEXCOORD2;
    nointerpolation float4 radii : TEXCOORD3;
    nointerpolation float2 size : TEXCOORD4;
    nointerpolation float4 clip : TEXCOORD5;
    nointerpolation uint flags : TEXCOORD6;
    float2 uv : TEXCOORD7;
    nointerpolation uint slot : TEXCOORD8;
    nointerpolation float spread : TEXCOORD9;
    nointerpolation float2 filterBc : TEXCOORD10; // brightness, contrast (1,1 = no-op)
};

// Mirrors Model.hpp uiUnpackTransform: pad0 = angle(lo16) | scaleX(hi16),
// pad1 = scaleY(lo16) | bright(lo8 of hi16) | contrast(hi8 of hi16).
void uiUnpackTransform(uint pad0, uint pad1, out float angle, out float scaleX, out float scaleY, out float bright, out float contrast) {
    angle = -180.0 + ((float)(pad0 & 0xFFFFu) / 65535.0) * 360.0;
    scaleX = ((float)(pad0 >> 16) / 65535.0) * 4.0;
    scaleY = ((float)(pad1 & 0xFFFFu) / 65535.0) * 4.0;
    bright = ((float)((pad1 >> 16) & 0xFFu) / 255.0) * 2.0;
    contrast = ((float)((pad1 >> 24) & 0xFFu) / 255.0) * 2.0;
}

static const float2 kCorner[6] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0),
};

// Same 4 corners, no repeats — mesh shader emits them once and indexes twice (quad = 2 tris).
static const float2 kQuadCorner[4] = {
    float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0),
};

// Shared by uiVs (vertex-pull path, always available) and uiMs (mesh-shader path, used when
// the device has VK_EXT_mesh_shader). Same primitive, same math, same VsOut — two ways to feed
// the GPU with it, not a second renderer: core/rhi/Device.cpp caps-gates which one gets bound.
VsOut uiPrimVertex(uint prim, float2 corner) {
    VsOut o;
    const uint base = kPrimBase + prim * 80;
    const float2 pos = asfloat(gUi.Load2(base));
    const float2 size = asfloat(gUi.Load2(base + 8));
    const float4 color = asfloat(gUi.Load4(base + 16));
    const uint2 packed = gUi.Load2(base + 32);
    const float radius = asfloat(gUi.Load(base + 48));
    const float4 extra = asfloat(gUi.Load4(base + 48));
    float4 radii = asfloat(gUi.Load4(base + 64));
    if (radii.x + radii.y + radii.z + radii.w <= 0.0) {
        radii = radius.xxxx;
    }
    const float clipH = asfloat(gUi.Load(base + 40));
    const float2 extent = asfloat(gUi.Load2(0));
    const bool ribbon = (packed.y & 256u) != 0;
    const bool transform = (packed.y & 512u) != 0;
    float2 filterBc = float2(1.0, 1.0);
    float2 xy;
    if (transform && !ribbon) {
        float angle, scaleX, scaleY, bright, contrast;
        uiUnpackTransform(gUi.Load(base + 40), gUi.Load(base + 44), angle, scaleX, scaleY, bright, contrast);
        filterBc = float2(bright, contrast);
        const float2 center = pos + size * 0.5;
        float2 off = (corner - 0.5) * size;
        off *= float2(scaleX, scaleY);
        const float rad = radians(angle);
        const float s = sin(rad);
        const float c = cos(rad);
        off = float2(off.x * c - off.y * s, off.x * s + off.y * c);
        xy = center + off;
    } else {
        xy = pos + corner * size;
    }
    float2 spine = pos;
    float2 capA = pos;
    float2 capB = pos;
    float halfW = 3.0;
    uint join = 1u;
    if (ribbon) {
        const float2 p0 = pos;
        const float2 p1 = size;
        const float2 p2 = extra.xy;
        const float2 p3 = extra.zw;
        const uint seg = packed.x & 31u;
        join = packed.x >> 8;
        const float steps = 16.0;
        const float t0 = (float)seg / steps;
        const float t1 = ((float)seg + 1.0) / steps;
        const float t = lerp(t0, t1, corner.y);
        const float u = 1.0 - t;
        const float2 b = u * u * u * p0 + 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t * p3;
        const float2 tang = 3.0 * u * u * (p1 - p0) + 6.0 * u * t * (p2 - p1) + 3.0 * t * t * (p3 - p2);
        const float tlen = max(length(tang), 0.0001);
        const float2 dir = tang / tlen;
        const float2 nrm = float2(-dir.y, dir.x);
        float reach = 0.0;
        if (join == 0u) {
            reach = halfW;
        } else if (join == 2u) {
            reach = halfW * 1.5;
        }
        const float side = corner.x * 2.0 - 1.0;
        const float along = (corner.y - 0.5) * 2.0;
        xy = b + nrm * side * halfW + dir * along * reach;
        spine = b;
        const float uA = 1.0 - t0;
        const float uB = 1.0 - t1;
        capA = uA * uA * uA * p0 + 3.0 * uA * uA * t0 * p1 + 3.0 * uA * t0 * t0 * p2 + t0 * t0 * t0 * p3;
        capB = uB * uB * uB * p0 + 3.0 * uB * uB * t1 * p1 + 3.0 * uB * t1 * t1 * p2 + t1 * t1 * t1 * p3;
    }
    const float2 ndc = float2(xy.x / extent.x * 2.0 - 1.0, xy.y / extent.y * 2.0 - 1.0);
    o.pos = float4(ndc, 0.0, 1.0);
    o.local = ribbon ? (xy - spine) : corner * size;
    o.origin = ribbon ? spine : pos;
    o.color = color;
    o.radii = ribbon ? float4(capA, capB) : radii;
    o.size = ribbon ? float2(halfW, (float)join) : size;
    o.clip = float4(extra.y, extra.z, extra.w, clipH);
    o.flags = packed.y;
    o.uv = float2(0.0, 0.0);
    o.slot = 0;
    o.spread = ribbon || transform ? 0.0 : asfloat(gUi.Load(base + 44));
    o.filterBc = filterBc;
    if (ribbon) {
        return o;
    }
    if ((packed.y & 32u) != 0) {
        o.uv = lerp(extra.xy, extra.zw, corner);
        o.radii = float4(6.0, 6.0, 6.0, 6.0);
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

// Vertex-pull path: always compiled, always available, the proven fallback. 6 verts/primitive,
// no index buffer, same as before this split — only the math moved into uiPrimVertex.
[shader("vertex")]
VsOut uiVs(uint vid : SV_VertexID) {
    const uint prim = vid / 6;
    const float2 corner = kCorner[vid % 6];
    return uiPrimVertex(prim, corner);
}

// Mesh-shader path: one workgroup per primitive (SV_GroupID.x == prim, set by the drawCount
// passed to vkCmdDrawMeshTasksEXT), 4 unique vertices + 2 indexed triangles instead of 6
// vertices with repeats. No task shader (VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT) — GPU-driven
// per-meshlet culling is a later step (plan Фаза 5/11), this step only moves the feed path.
[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void uiMs(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices VsOut verts[4],
    out indices uint3 prims[2]) {
    if (gtid == 0) {
        SetMeshOutputCounts(4, 2);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < 4) {
        verts[gtid] = uiPrimVertex(gid, kQuadCorner[gtid]);
    }
    if (gtid == 0) {
        prims[0] = uint3(0, 1, 2);
        prims[1] = uint3(0, 2, 3);
    }
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

float sdRound(float2 local, float2 size, float4 rad) {
    const float2 p = local - size * 0.5;
    float r = rad.x;
    if (p.x >= 0.0 && p.y < 0.0) {
        r = rad.y;
    } else if (p.x >= 0.0 && p.y >= 0.0) {
        r = rad.z;
    } else if (p.x < 0.0 && p.y >= 0.0) {
        r = rad.w;
    }
    r = min(r, min(size.x, size.y) * 0.5);
    const float2 halfSize = size * 0.5;
    const float2 q = abs(p) - (halfSize - r);
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
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
        const float d = sdRound(i.local, i.size, i.radii);
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
    } else if ((i.flags & 256u) != 0) {
        const float2 pix = i.origin + i.local;
        const float halfW = max(i.size.x, 0.5);
        if (i.size.y < 0.5) {
            const float2 ab = i.radii.zw - i.radii.xy;
            const float denom = max(dot(ab, ab), 0.0001);
            const float t = saturate(dot(pix - i.radii.xy, ab) / denom);
            const float dist = length(pix - (i.radii.xy + ab * t));
            const float aa = max(fwidth(dist), 0.0001);
            cov = saturate(0.5 - (dist - halfW) / aa);
        } else {
            cov = 1.0;
        }
    } else if ((i.flags & 1u) != 0) {
        const float sd = gSdf.SampleLevel(gSdfSamp, i.uv, 0.0).r;
        const float w = clamp(fwidth(sd), 0.015, 0.06);
        cov = smoothstep(0.5 - w, 0.5 + w, sd);
    } else {
        const float d = sdRound(i.local, i.size, i.radii);
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
    if ((i.flags & 512u) != 0) {
        rgb = (rgb - 0.5) * i.filterBc.y + 0.5;
        rgb *= i.filterBc.x;
    }
    float4 c = float4(rgb, alpha);
    c.a *= cov;
    return c;
}
