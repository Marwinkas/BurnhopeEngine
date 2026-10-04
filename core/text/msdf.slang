// One mesh group is one glyph: 4 vertices, 2 triangles. No CPU vertex buffer.
// posMin/posMax are clip coordinates from textLayoutLine. The fragment only samples the atlas.

struct GlyphInstance {
    float2 posMin;
    float2 posMax;
    float2 uvMin;
    float2 uvMax;
    uint color;
    float pxRange;
    uint2 pad;
};

struct GlyphDrawPush {
    GlyphInstance* glyphs;
    uint count;
    uint reserved;
};

[[vk::push_constant]]
GlyphDrawPush gGlyphs;

[[vk::binding(0, 0)]]
Texture2D<float4> gAtlas;

[[vk::binding(0, 1)]]
SamplerState gSamp;

float msdfMedian(float3 msd) {
    return max(min(msd.r, msd.g), min(max(msd.r, msd.g), msd.b));
}

float msdfOpacity(float3 msd, float pxRange) {
    const float sd = msdfMedian(msd);
    const float screenPxDistance = pxRange * (sd - 0.5);
    return clamp(screenPxDistance + 0.5, 0.0, 1.0);
}

struct GlyphOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint color : COLOR0;
    nointerpolation float pxRange : COLOR1;
};

[shader("mesh")]
[numthreads(4, 1, 1)]
[outputtopology("triangle")]
void glyphMs(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices GlyphOut verts[4],
    out indices uint3 prims[2])
{
    if (gtid == 0) {
        SetMeshOutputCounts(4, 2);
    }
    GroupMemoryBarrierWithGroupSync();
    const GlyphInstance g = gGlyphs.glyphs[gid];
    const float2 corner[4] = {
        g.posMin,
        float2(g.posMax.x, g.posMin.y),
        g.posMax,
        float2(g.posMin.x, g.posMax.y)
    };
    const float2 uv[4] = {
        g.uvMin,
        float2(g.uvMax.x, g.uvMin.y),
        g.uvMax,
        float2(g.uvMin.x, g.uvMax.y)
    };
    if (gtid < 4) {
        verts[gtid].pos = float4(corner[gtid], 0.0, 1.0);
        verts[gtid].uv = uv[gtid];
        verts[gtid].color = g.color;
        verts[gtid].pxRange = g.pxRange;
    }
    if (gtid == 0) {
        prims[0] = uint3(0, 1, 2);
        prims[1] = uint3(0, 2, 3);
    }
}

[shader("fragment")]
float4 glyphFs(GlyphOut vin) : SV_Target0 {
    const float3 msd = gAtlas.Sample(gSamp, vin.uv).rgb;
    const float opacity = msdfOpacity(msd, vin.pxRange);
    const float r = float(vin.color & 255) / 255.0;
    const float g = float((vin.color >> 8) & 255) / 255.0;
    const float b = float((vin.color >> 16) & 255) / 255.0;
    const float a = float((vin.color >> 24) & 255) / 255.0;
    return float4(r, g, b, a * opacity);
}
