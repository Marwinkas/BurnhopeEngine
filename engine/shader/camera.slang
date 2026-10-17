// Screen to world. Included after frame.slang. Passes do not each keep a copy.

float3 reconstructWorld(float2 uv, float depth) {
    const float2 ndc = float2(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0);
    float4 world = mul(gFrame.invViewProj, float4(ndc, depth, 1.0));
    return world.xyz / max(world.w, 1e-6);
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
    n = dot(n, n) > 1e-12 ? normalize(n) : view;
    if (dot(n, view) < 0.0) {
        n = -n;
    }
    return n;
}

float3 reconstructNormalFromDepth(Texture2D<float> depthTex, uint2 px, float3 P) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 ip = int2(px);
    const float dc = depthTex.Load(int3(ip, 0));
    const int2 pL = int2(max(ip.x - 1, 0), ip.y);
    const int2 pR = int2(min(ip.x + 1, ext.x), ip.y);
    const int2 pU = int2(ip.x, max(ip.y - 1, 0));
    const int2 pD = int2(ip.x, min(ip.y + 1, ext.y));
    const float dL = depthTex.Load(int3(pL, 0));
    const float dR = depthTex.Load(int3(pR, 0));
    const float dU = depthTex.Load(int3(pU, 0));
    const float dD = depthTex.Load(int3(pD, 0));
    const bool useL = abs(dL - dc) < abs(dR - dc) && dL > 0.0;
    const bool useU = abs(dU - dc) < abs(dD - dc) && dU > 0.0;
    const float3 ex = useL ? (P - reconstructWorld(uint2(pL), dL)) : (reconstructWorld(uint2(pR), dR) - P);
    const float3 ey = useU ? (P - reconstructWorld(uint2(pU), dU)) : (reconstructWorld(uint2(pD), dD) - P);
    return orientNormal(cross(ex, ey), normalize(gFrame.cameraPos.xyz - P));
}

float3 reconstructNormalGated(Texture2D<float> depthTex, uint2 px, float3 P, float maxDist2) {
    (void)maxDist2;
    return reconstructNormalFromDepth(depthTex, px, P);
}