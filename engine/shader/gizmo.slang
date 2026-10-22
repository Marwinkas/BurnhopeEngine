// Манипулятор и значки акторов. Один mesh-shader, кусок на группу: лимит выхода 256/256.

struct GizmoPush {
    uint phase;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
GizmoPush gPhase;

struct GizmoIn {
    float4x4 viewProj;
    float4 origin;
    float4 ax;
    float4 ay;
    float4 az;
    float4 cam;
    float4 misc;
    float4 marker[8];
    float4 state;
    float4 meshOf;
};

[[vk::binding(47, 0)]] ConstantBuffer<GizmoIn> gGizmo;

struct VertOut {
    float4 pos : SV_Position;
    float3 color : COLOR0;
};

groupshared VertOut gVert[256];
groupshared uint3 gPrim[256];
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

float3 axisAlbedo(int axis) {
    if (axis == 0) {
        return float3(0.93, 0.28, 0.22);
    }
    if (axis == 1) {
        return float3(0.35, 0.86, 0.38);
    }
    return float3(0.28, 0.48, 0.95);
}

float3 partColor(int part, float3 albedo) {
    const int hot = int(gGizmo.state.x + 0.5);
    const int act = int(gGizmo.state.y + 0.5);
    if (act == part) {
        return float3(1.0, 0.92, 0.12);
    }
    if (hot == part) {
        return min(albedo * 1.3, float3(1.0, 1.0, 1.0));
    }
    return albedo;
}

float3 shade(float3 n, float3 albedo) {
    float3 to = gGizmo.cam.xyz - gGizmo.origin.xyz;
    if (dot(to, to) < 1e-8) {
        to = float3(0.0, 1.0, 0.0);
    }
    const float ndl = saturate(dot(normalize(n), normalize(to)));
    return albedo * (0.38 + 0.62 * ndl);
}

float pxAt(float3 p) {
    return max(length(p - gGizmo.cam.xyz), 0.2) * gGizmo.cam.w;
}

float iconHalf(float3 p) {
    const float dist = max(length(p - gGizmo.cam.xyz), 0.2);
    const float t = saturate((dist - 8.0) / 40.0);
    const float px = clamp(lerp(72.0, 36.0, t), 32.0, 80.0);
    return pxAt(p) * px * 0.5;
}

void frameOf(float3 dir, out float3 u, out float3 v) {
    const float3 ref = abs(dir.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    u = normalize(cross(dir, ref));
    v = normalize(cross(dir, u));
}

uint pushVert(float3 p, float3 col) {
    if (gNv >= 256) {
        return 0;
    }
    const uint id = gNv;
    gVert[gNv].pos = mul(gGizmo.viewProj, float4(p, 1.0));
    gVert[gNv].color = col;
    gNv += 1;
    return id;
}

void pushTri(uint a, uint b, uint c) {
    if (gNp >= 256) {
        return;
    }
    gPrim[gNp] = uint3(a, b, c);
    gNp += 1;
}

void emitTri(float3 a, float3 b, float3 c, float3 col) {
    if (gNv + 3 > 256 || gNp + 1 > 256) {
        return;
    }
    const uint base = gNv;
    pushVert(a, col);
    pushVert(b, col);
    pushVert(c, col);
    pushTri(base, base + 1, base + 2);
}

void emitQuad(float3 a, float3 b, float3 c, float3 d, float3 col) {
    emitTri(a, b, c, col);
    emitTri(a, c, d, col);
}

void emitBillboard(float3 p, float half, float3 col) {
    float3 toCam = gGizmo.cam.xyz - p;
    if (dot(toCam, toCam) < 1e-8) {
        toCam = float3(0.0, 0.0, 1.0);
    }
    toCam = normalize(toCam);
    float3 up = float3(0.0, 1.0, 0.0);
    if (abs(dot(up, toCam)) > 0.95) {
        up = float3(1.0, 0.0, 0.0);
    }
    float3 right = normalize(cross(up, toCam)) * half;
    up = normalize(cross(toCam, right)) * half;
    emitQuad(p - right - up, p + right - up, p + right + up, p - right + up, col);
}

void emitFace(float3 c0, float3 c1, float3 c2, float3 c3, float3 n, float3 albedo) {
    emitQuad(c0, c1, c2, c3, shade(n, albedo));
}

void emitCube(float3 o, float3 ax, float3 ay, float3 az, float hx, float hy, float hz, float3 albedo) {
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
    emitFace(c001, c101, c111, c011, az, albedo);
    emitFace(c100, c000, c010, c110, -az, albedo);
    emitFace(c010, c011, c111, c110, ay, albedo);
    emitFace(c000, c001, c101, c100, -ay, albedo);
    emitFace(c100, c110, c111, c101, ax, albedo);
    emitFace(c000, c100, c101, c001, -ax, albedo);
}

void emitEdge(float3 a, float3 b, float width, float3 col) {
    float3 dir = b - a;
    float3 n = cross(dir, gGizmo.cam.xyz - a);
    if (dot(n, n) < 1e-8) {
        n = cross(dir, float3(0.0, 1.0, 0.0));
    }
    const float3 s = normalize(n) * width;
    emitQuad(a - s, a + s, b + s, b - s, col);
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

void emitCone(float3 origin, float3 dir, float radius, float height, float3 albedo) {
    float3 u;
    float3 v;
    frameOf(dir, u, v);
    const float3 apex = origin + dir * height;
    const uint apexId = pushVert(apex, shade(dir, albedo));
    const uint center = pushVert(origin, shade(-dir, albedo));
    uint ring[16];
    [unroll]
    for (int s = 0; s < 16; ++s) {
        const float a = float(s) * 0.39269908;
        const float3 radial = u * cos(a) + v * sin(a);
        const float3 p = origin + radial * radius;
        const float3 n = normalize(radial * height + dir * radius);
        ring[s] = pushVert(p, shade(n, albedo));
    }
    [unroll]
    for (int s = 0; s < 16; ++s) {
        const uint n = (s + 1) % 16;
        pushTri(apexId, ring[s], ring[n]);
        pushTri(center, ring[n], ring[s]);
    }
}

void emitTorus(int axis, int half) {
    const float len = max(gGizmo.misc.x, 0.05);
    const float major = len;
    const float minor = len * 0.045;
    const float3 dir = axisDir(axis);
    float3 u;
    float3 v;
    frameOf(dir, u, v);
    const float3 o = gGizmo.origin.xyz;
    const float3 albedo = partColor(axis, axisAlbedo(axis));
    const int steps = 16;
    const int segs = 8;
    const int start = half * steps;
    uint id[17][8];
    for (int ring = 0; ring <= steps; ++ring) {
        const float a = float(start + ring) * 0.19634954;
        const float3 arm = u * cos(a) + v * sin(a);
        const float3 center = o + arm * major;
        for (int m = 0; m < segs; ++m) {
            const float b = float(m) * 0.78539816;
            const float3 nrm = arm * cos(b) + dir * sin(b);
            id[ring][m] = pushVert(center + nrm * minor, shade(nrm, albedo));
        }
    }
    for (int ring = 0; ring < steps; ++ring) {
        for (int m = 0; m < segs; ++m) {
            const int m1 = (m + 1) % segs;
            const uint a = id[ring][m];
            const uint b = id[ring][m1];
            const uint c = id[ring + 1][m];
            const uint d = id[ring + 1][m1];
            pushTri(a, c, d);
            pushTri(a, d, b);
        }
    }
}

void emitShaft(int axis, float len) {
    const float3 dir = axisDir(axis);
    float3 u;
    float3 v;
    frameOf(dir, u, v);
    const float3 o = gGizmo.origin.xyz;
    const float a0 = 0.16;
    const float a1 = 0.70;
    const float3 mid = o + dir * ((a0 + a1) * 0.5 * len);
    const float halfL = (a1 - a0) * 0.5 * len;
    const float rad = len * 0.035;
    emitCube(mid, dir, u, v, halfL, rad, rad, partColor(axis, axisAlbedo(axis)));
}

void emitTip(int axis, int mode, float len) {
    const float3 dir = axisDir(axis);
    const float3 o = gGizmo.origin.xyz;
    const float3 albedo = partColor(axis, axisAlbedo(axis));
    if (mode == 2) {
        float3 u;
        float3 v;
        frameOf(dir, u, v);
        const float half = len * 0.09;
        const float3 c = o + dir * (len * 0.84);
        emitCube(c, dir, u, v, half, half, half, albedo);
        return;
    }
    const float3 base = o + dir * (len * 0.70);
    emitCone(base, dir, len * 0.16, len * 0.30, albedo);
}

void emitPlane(int missing, float len) {
    const float at = len * 0.32;
    const float half = len * 0.10;
    float3 o = gGizmo.origin.xyz;
    float3 axes[3];
    axes[0] = gGizmo.ax.xyz;
    axes[1] = gGizmo.ay.xyz;
    axes[2] = gGizmo.az.xyz;
    [unroll]
    for (int a = 0; a < 3; ++a) {
        if (a == missing) {
            continue;
        }
        o += axes[a] * at;
    }
    const int part = 3 + missing;
    emitCube(o, axes[0], axes[1], axes[2], half, half, half, partColor(part, axisAlbedo(missing)));
}

void emitCenter(float len) {
    const float half = len * 0.10;
    emitCube(gGizmo.origin.xyz, gGizmo.ax.xyz, gGizmo.ay.xyz, gGizmo.az.xyz, half, half, half, partColor(6, float3(0.96, 0.96, 0.96)));
}

void emitMarkers() {
    const int count = min(int(gGizmo.misc.w + 0.5), 8);
    const float3 home = gGizmo.origin.xyz;
    const bool live = gGizmo.origin.w > 0.5;
    for (int i = 0; i < 8; ++i) {
        if (i >= count) {
            break;
        }
        const float3 p = gGizmo.marker[i].xyz;
        const int kind = int(gGizmo.marker[i].w + 0.5);
        if (kind == 3) {
            continue;
        }
        if (live && dot(p - home, p - home) < 0.04) {
            continue;
        }
        const float half = iconHalf(p);
        if (kind == 1) {
            emitBillboard(p, half, float3(0.95, 0.62, 0.18));
            emitBillboard(p, half * 0.42, float3(1.0, 0.9, 0.45));
        } else {
            emitBillboard(p, half * 0.78, float3(0.55, 0.62, 0.72));
            emitBillboard(p, half * 0.34, float3(0.95, 0.78, 0.42));
        }
    }
}

[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void gizmoMesh(uint gid : SV_GroupID, out vertices VertOut verts[256], out indices uint3 prims[256]) {
    gNv = 0;
    gNp = 0;
    if ((gPhase.phase == 0u && gid > 1u) || (gPhase.phase != 0u && gid < 2u)) {
        SetMeshOutputCounts(0, 0);
        return;
    }
    const bool live = gGizmo.origin.w > 0.5;
    const int mode = int(gGizmo.misc.y + 0.5);
    const float len = max(gGizmo.misc.x, 0.05);
    const float3 boxO = gGizmo.meshOf.w > 0.5 ? gGizmo.meshOf.xyz : gGizmo.origin.xyz;
    if (gid == 0) {
        emitMarkers();
    } else if (gid == 1 && live && gGizmo.ax.w > 0.001 && gGizmo.ay.w > 0.001 && gGizmo.az.w > 0.001) {
        emitBox(boxO, gGizmo.ax.xyz, gGizmo.ay.xyz, gGizmo.az.xyz, gGizmo.ax.w, gGizmo.ay.w, gGizmo.az.w, pxAt(boxO) * 1.4, float3(0.95, 0.86, 0.42));
    } else if (live && mode < 3 && gid == 2 && mode != 1) {
        emitShaft(0, len);
        emitShaft(1, len);
        emitShaft(2, len);
    } else if (live && mode < 3 && gid == 3 && mode != 1) {
        emitTip(0, mode, len);
        emitTip(1, mode, len);
        emitTip(2, mode, len);
    } else if (live && mode < 3 && gid == 4 && mode != 1) {
        emitPlane(0, len);
        emitPlane(1, len);
        emitPlane(2, len);
        emitCenter(len);
    } else if (live && mode == 1 && gid >= 5 && gid <= 10) {
        const int axis = int(gid - 5) / 2;
        const int half = int(gid - 5) % 2;
        if (!(gGizmo.state.z > 0.5 && axis == 2)) {
            emitTorus(axis, half);
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
