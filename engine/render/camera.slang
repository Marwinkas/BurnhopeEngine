// Screen to world. Included after frame.slang. Passes do not each keep a copy.

float3 reconstructWorld(float2 uv, float depth) {
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 world = mul(gFrame.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / world.w;
}

float3 reconstructWorld(uint2 px, float depth) {
    return reconstructWorld((float2(px) + 0.5) * gFrame.invExtent, depth);
}

float4 worldToClip(float3 world) {
    return mul(gFrame.viewProj, float4(world, 1.0));
}

float4 worldToSun(uint cascade, float3 world) {
    return mul(gSun.sunCascade[cascade], float4(world, 1.0));
}

float3 normalFromEdges(float3 ex, float3 ey) {
    const float3 n = cross(ex, ey);
    return dot(n, n) > 1e-12 ? normalize(n) : float3(0.0, 1.0, 0.0);
}

float3 boxProjectDir(float3 pos, float3 dir) {
    const float3 d = dot(dir, dir) > 1e-8 ? normalize(dir) : float3(0.0, 1.0, 0.0);
    const float3 bmin = gSun.cubeMin.xyz;
    const float3 bmax = gSun.cubeMax.xyz;
    const float3 inv = 1.0 / d;
    const float3 t0 = (bmin - pos) * inv;
    const float3 t1 = (bmax - pos) * inv;
    const float3 tbg = max(t0, t1);
    const float t = min(tbg.x, min(tbg.y, tbg.z));
    return normalize(pos + d * max(t, 0.05) - (bmin + bmax) * 0.5);
}

float3 orientNormal(float3 n, float3 view) {
    n = dot(n, n) > 1e-12 ? normalize(n) : float3(0.0, 1.0, 0.0);
    if (dot(n, view) < 0.0) {
        n = -n;
    }
    return n;
}

float3 reconstructNormalFromDepth(Texture2D<float> depthTex, uint2 px, float3 P) {
    const uint2 ext = gFrame.extent.xy;
    const uint2 ax = uint2(min(px.x + 1u, ext.x - 1u), px.y);
    const uint2 ay = uint2(px.x, min(px.y + 1u, ext.y - 1u));
    const float3 ex = reconstructWorld(ax, depthTex.Load(int3(ax, 0))) - P;
    const float3 ey = reconstructWorld(ay, depthTex.Load(int3(ay, 0))) - P;
    return orientNormal(cross(ex, ey), normalize(gFrame.cameraPos.xyz - P));
}

float3 reconstructNormalGated(Texture2D<float> depthTex, uint2 px, float3 P, float maxDist2) {
    const uint2 ext = gFrame.extent.xy;
    const uint2 ax = uint2(min(px.x + 1u, ext.x - 1u), px.y);
    const uint2 ay = uint2(px.x, min(px.y + 1u, ext.y - 1u));
    float3 ex = reconstructWorld(ax, depthTex.Load(int3(ax, 0))) - P;
    float3 ey = reconstructWorld(ay, depthTex.Load(int3(ay, 0))) - P;
    if (dot(ex, ex) > maxDist2) {
        ex = 0.0;
    }
    if (dot(ey, ey) > maxDist2) {
        ey = 0.0;
    }
    return orientNormal(cross(ex, ey), normalize(gFrame.cameraPos.xyz - P));
}
