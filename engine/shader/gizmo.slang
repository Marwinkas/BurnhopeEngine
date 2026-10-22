// Оси выбранного объекта и короткие метки остальных. Один mesh-draw, без дескрипторов соседа.

struct GizmoIn {
    float4x4 viewProj;
    float4 origin;
    float4 ax;
    float4 ay;
    float4 az;
    float4 cam;
    float4 misc;
    float4 marker[8];
};

[[vk::binding(47, 0)]] ConstantBuffer<GizmoIn> gGizmo;

struct VertOut {
    float4 pos : SV_Position;
    float3 color : COLOR0;
};

groupshared VertOut gVert[256];
groupshared uint3 gPrim[128];
groupshared uint gNv;
groupshared uint gNp;

float3 axisDir(int axis) {
    if (axis == 0) {
        return gGizmo.ax.xyz;
    }
    if (axis == 1) {
        return gGizmo.ay.xyz;
    }
    return gGizmo.az.xyz;
}

float3 axisColor(int axis) {
    const int hot = int(gGizmo.misc.z + 0.5);
    if (hot == axis) {
        return float3(1.0, 0.82, 0.25);
    }
    if (axis == 0) {
        return float3(0.93, 0.28, 0.22);
    }
    if (axis == 1) {
        return float3(0.35, 0.86, 0.38);
    }
    return float3(0.28, 0.48, 0.95);
}

float pxAt(float3 p) {
    return max(length(p - gGizmo.cam.xyz), 0.2) * gGizmo.cam.w;
}

float3 sideOf(float3 dir, float3 at, float width) {
    float3 n = cross(dir, gGizmo.cam.xyz - at);
    if (dot(n, n) < 1e-8) {
        n = cross(dir, float3(0.0, 1.0, 0.0));
    }
    return normalize(n) * width;
}

void emitQuad(float3 a, float3 b, float3 c, float3 d, float3 col) {
    if (gNv + 4 > 256 || gNp + 2 > 128) {
        return;
    }
    const uint base = gNv;
    const float3 p[4] = {a, b, c, d};
    [unroll]
    for (int i = 0; i < 4; ++i) {
        gVert[gNv].pos = mul(gGizmo.viewProj, float4(p[i], 1.0));
        gVert[gNv].color = col;
        gNv += 1;
    }
    gPrim[gNp] = uint3(base, base + 1, base + 2);
    gPrim[gNp + 1] = uint3(base, base + 2, base + 3);
    gNp += 2;
}

void emitBillboard(float3 p, float3 localUp, float halfW, float halfH, float3 col) {
    float3 toCam = gGizmo.cam.xyz - p;
    if (dot(toCam, toCam) < 1e-8) {
        toCam = float3(0.0, 0.0, 1.0);
    }
    float3 right = cross(localUp, toCam);
    if (dot(right, right) < 1e-8) {
        right = cross(float3(1.0, 0.0, 0.0), toCam);
    }
    right = normalize(right) * halfW;
    float3 up = normalize(cross(toCam, right)) * halfH;
    emitQuad(p - right - up, p + right - up, p + right + up, p - right + up, col);
}

void emitShaft(float3 from, float3 to, float width, float3 col) {
    const float3 dir = to - from;
    const float3 s = sideOf(dir, from, width);
    emitQuad(from - s, from + s, to + s, to - s, col);
}

void emitEdge(float3 a, float3 b, float width, float3 col) {
    emitShaft(a, b, width, col);
}

void emitBox(float3 o, float3 ax, float3 ay, float3 az, float hx, float hy, float hz, float width, float3 col) {
    const float3 x = ax * hx;
    const float3 y = ay * hy;
    const float3 z = az * hz;
    const float3 c000 = o - x - y - z;
    const float3 c100 = o + x - y - z;
    const float3 c010 = o - x + y - z;
    const float3 c110 = o + x + y - z;
    const float3 c001 = o - x - y + z;
    const float3 c101 = o + x - y + z;
    const float3 c011 = o - x + y + z;
    const float3 c111 = o + x + y + z;
    emitEdge(c000, c100, width, col);
    emitEdge(c010, c110, width, col);
    emitEdge(c001, c101, width, col);
    emitEdge(c011, c111, width, col);
    emitEdge(c000, c010, width, col);
    emitEdge(c100, c110, width, col);
    emitEdge(c001, c011, width, col);
    emitEdge(c101, c111, width, col);
    emitEdge(c000, c001, width, col);
    emitEdge(c100, c101, width, col);
    emitEdge(c010, c011, width, col);
    emitEdge(c110, c111, width, col);
}

void emitRing(int axis, float radius, float width, float3 col) {
    const float3 dir = axisDir(axis);
    float3 u = cross(dir, abs(dir.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0));
    u = normalize(u);
    const float3 v = normalize(cross(dir, u));
    const float3 o = gGizmo.origin.xyz;
    [unroll]
    for (int s = 0; s < 12; ++s) {
        const float a0 = float(s) * 0.5235988;
        const float a1 = float(s + 1) * 0.5235988;
        const float3 p0 = o + (u * cos(a0) + v * sin(a0)) * radius;
        const float3 p1 = o + (u * cos(a1) + v * sin(a1)) * radius;
        const float3 side = sideOf(p1 - p0, p0, width);
        emitQuad(p0 - side, p0 + side, p1 + side, p1 - side, col);
    }
}

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void gizmoMesh(out vertices VertOut verts[256], out indices uint3 prims[128]) {
    gNv = 0;
    gNp = 0;
    const int count = min(int(gGizmo.misc.w + 0.5), 8);
    for (int i = 0; i < 8; ++i) {
        if (i >= count) {
            break;
        }
        const float3 p = gGizmo.marker[i].xyz;
        const int kind = int(gGizmo.marker[i].w + 0.5);
        if (kind == 3) {
            continue;
        }
        const float mark = pxAt(p) * 9.0;
        const bool here = gGizmo.origin.w > 0.5 && dot(p - gGizmo.origin.xyz, p - gGizmo.origin.xyz) < 0.02;
        const float3 faceUp = here ? gGizmo.ay.xyz : float3(0.0, 1.0, 0.0);
        if (kind == 1) {
            emitBillboard(p, faceUp, mark * 0.95, mark * 0.95, float3(0.95, 0.62, 0.18));
            emitBillboard(p, faceUp, mark * 0.38, mark * 0.38, float3(1.0, 0.9, 0.45));
        } else {
            emitBillboard(p, faceUp, mark * 0.7, mark * 0.7, float3(0.55, 0.62, 0.72));
            emitBillboard(p, faceUp, mark * 0.28, mark * 0.28, float3(0.95, 0.78, 0.42));
        }
    }
    if (gGizmo.origin.w > 0.5 && gGizmo.ax.w > 0.001 && gGizmo.ay.w > 0.001 && gGizmo.az.w > 0.001) {
        const float3 o = gGizmo.origin.xyz;
        emitBox(o, gGizmo.ax.xyz, gGizmo.ay.xyz, gGizmo.az.xyz, gGizmo.ax.w, gGizmo.ay.w, gGizmo.az.w, pxAt(o) * 1.4, float3(0.95, 0.86, 0.42));
    }
    if (gGizmo.origin.w > 0.5) {
        const float hub = pxAt(gGizmo.origin.xyz) * 7.0;
        const float3 o = gGizmo.origin.xyz;
        emitBillboard(o, gGizmo.ay.xyz, hub * 0.45, hub * 0.45, float3(0.96, 0.96, 0.96));
        emitShaft(o, o + gGizmo.ax.xyz * hub, hub * 0.12, float3(0.93, 0.28, 0.22));
        emitShaft(o, o + gGizmo.ay.xyz * hub, hub * 0.12, float3(0.35, 0.86, 0.38));
        emitShaft(o, o + gGizmo.az.xyz * hub, hub * 0.12, float3(0.28, 0.48, 0.95));
    }
    if (gGizmo.origin.w > 0.5) {
        const float len = max(gGizmo.misc.x, 0.05);
        const float width = pxAt(gGizmo.origin.xyz) * 2.2;
        const int mode = int(gGizmo.misc.y + 0.5);
        const float3 o = gGizmo.origin.xyz;
        [unroll]
        for (int axis = 0; axis < 3; ++axis) {
            const float3 dir = axisDir(axis);
            const float3 col = axisColor(axis);
            if (mode == 1) {
                emitRing(axis, len, width, col);
            } else {
                const float tip = mode == 2 ? len * 0.82 : len * 0.78;
                emitShaft(o, o + dir * tip, width, col);
                const float3 end = o + dir * len;
                const float3 neck = o + dir * tip;
                emitShaft(neck, end, width * (mode == 2 ? 2.6 : 2.2), col);
            }
        }
    }
    SetMeshOutputCounts(gNv, gNp);
    for (uint i = 0; i < gNv; ++i) {
        verts[i] = gVert[i];
    }
    for (uint i = 0; i < gNp; ++i) {
        prims[i] = gPrim[i];
    }
}

[shader("fragment")]
float4 gizmoFs(VertOut vin) : SV_Target0 {
    return float4(vin.color, 1.0);
}
