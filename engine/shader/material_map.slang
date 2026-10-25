// UV transform and world-aligned triplanar weights. Sampling stays in the caller.

static const uint kMatWorldBit = 256u;
static const uint kMatFlipBit = 512u;
static const uint kMatClampBit = 2048u;

float matSafe(float v, float fallback) {
    return v > 1e-4 ? v : fallback;
}

float2 matTiling(MaterialGpu mat) {
    float2 t = float2(mat.uvScaleX, mat.uvScaleY);
    if (t.x < 1e-4 && t.y < 1e-4) {
        t = float2(1.0, 1.0);
    }
    return t;
}

float2 matRotate(float2 uv, float ang, float2 ddx, float2 ddy, out float2 rdx, out float2 rdy) {
    float s;
    float c;
    sincos(ang, s, c);
    const float2 o = uv - 0.5;
    const float2 r = float2(c * o.x - s * o.y, s * o.x + c * o.y) + 0.5;
    rdx = float2(c * ddx.x - s * ddx.y, s * ddx.x + c * ddx.y);
    rdy = float2(c * ddy.x - s * ddy.y, s * ddy.x + c * ddy.y);
    return r;
}

struct MatMap {
    uint world;
    float2 uv;
    float2 ddx;
    float2 ddy;
    float3 w;
    float2 uvX;
    float2 uvY;
    float2 uvZ;
    float2 dxX;
    float2 dyX;
    float2 dxY;
    float2 dyY;
    float2 dxZ;
    float2 dyZ;
    float3 axisSign;
    float normalScale;
    uint flipY;
};

MatMap matMapUv(MaterialGpu mat, float2 meshUv, float2 meshDx, float2 meshDy, float3 world, float3 dWx, float3 dWy, float3 geoN) {
    MatMap m;
    m.world = 0u;
    m.uv = meshUv;
    m.ddx = meshDx;
    m.ddy = meshDy;
    m.w = float3(0.0, 1.0, 0.0);
    m.uvX = meshUv;
    m.uvY = meshUv;
    m.uvZ = meshUv;
    m.dxX = meshDx;
    m.dyX = meshDy;
    m.dxY = meshDx;
    m.dyY = meshDy;
    m.dxZ = meshDx;
    m.dyZ = meshDy;
    m.axisSign = float3(1.0, 1.0, 1.0);
    m.normalScale = matSafe(mat.normalScale, 1.0);
    m.flipY = (mat.flags & kMatFlipBit) != 0u ? 1u : 0u;
    const float2 tile = matTiling(mat);
    const float2 off = float2(mat.uvOffX, mat.uvOffY);
    if ((mat.flags & kMatWorldBit) == 0u) {
        float2 rdx;
        float2 rdy;
        const float2 turned = matRotate(meshUv, mat.uvRotation, meshDx, meshDy, rdx, rdy);
        m.uv = turned * tile + off;
        m.ddx = rdx * tile;
        m.ddy = rdy * tile;
        if ((mat.flags & kMatClampBit) != 0u) {
            m.uv = clamp(m.uv, 0.0, 1.0);
            m.ddx = 0.0;
            m.ddy = 0.0;
        }
        return m;
    }
    const float grid = max(mat.worldGrid, 0.001);
    const float3 p = (world + float3(mat.worldOffX, mat.worldOffY, mat.worldOffZ)) / grid;
    const float3 ddxP = dWx / grid;
    const float3 ddyP = dWy / grid;
    m.world = 1u;
    m.uvX = p.zy * tile + off;
    m.uvY = p.xz * tile + off;
    m.uvZ = p.xy * tile + off;
    m.dxX = ddxP.zy * tile;
    m.dyX = ddyP.zy * tile;
    m.dxY = ddxP.xz * tile;
    m.dyY = ddyP.xz * tile;
    m.dxZ = ddxP.xy * tile;
    m.dyZ = ddyP.xy * tile;
    m.uv = m.uvY;
    m.ddx = m.dxY;
    m.ddy = m.dyY;
    float sharp = mat.triSharp;
    if (sharp < 1.0) {
        sharp = 4.0;
    }
    float3 w = pow(abs(geoN), sharp);
    const float sum = max(w.x + w.y + w.z, 1e-5);
    m.w = w / sum;
    m.axisSign = sign(geoN);
    if (abs(m.axisSign.x) < 1e-4) {
        m.axisSign.x = 1.0;
    }
    if (abs(m.axisSign.y) < 1e-4) {
        m.axisSign.y = 1.0;
    }
    if (abs(m.axisSign.z) < 1e-4) {
        m.axisSign.z = 1.0;
    }
    return m;
}

float3 matTriNormal(float3 nX, float3 nY, float3 nZ, float3 w, float3 axisSign) {
    nX = float3(nX.z * axisSign.x, nX.y, nX.x);
    nY = float3(nY.x, nY.z * axisSign.y, nY.y);
    nZ = float3(nZ.x, nZ.y, nZ.z * axisSign.z);
    if (w.x < 0.01) {
        nX = float3(0.0, 0.0, 1.0);
    }
    if (w.y < 0.01) {
        nY = float3(0.0, 0.0, 1.0);
    }
    if (w.z < 0.01) {
        nZ = float3(0.0, 0.0, 1.0);
    }
    const float3 blended = nX * w.x + nY * w.y + nZ * w.z;
    return dot(blended, blended) > 1e-8 ? normalize(blended) : float3(0.0, 1.0, 0.0);
}
