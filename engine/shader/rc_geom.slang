// Треугольник в буфере печи: 48 байт. Поверхность клетки — проекция её центра на треугольник.
// Камера в выборе точки не участвует.

struct RcHitVert {
    float3 p;
    float u;
    float3 n;
    float v;
    uint mat;
};

RcHitVert rcVert(uint vertex) {
    const uint o = rcPosByte() + vertex * 48u;
    RcHitVert r;
    r.p = float3(asfloat(rcLoad(o)), asfloat(rcLoad(o + 4u)), asfloat(rcLoad(o + 8u)));
    r.u = asfloat(rcLoad(o + 12u));
    r.n = float3(asfloat(rcLoad(o + 16u)), asfloat(rcLoad(o + 20u)), asfloat(rcLoad(o + 24u)));
    r.v = asfloat(rcLoad(o + 28u));
    r.mat = rcLoad(o + 32u);
    return r;
}

bool rcProject(uint tri, float3 center, float limit, out float3 P, out float3 N, out float dist) {
    P = center;
    N = float3(0.0, 1.0, 0.0);
    dist = 1e9;
    const RcHitVert a = rcVert(tri * 3u);
    const RcHitVert b = rcVert(tri * 3u + 1u);
    const RcHitVert c = rcVert(tri * 3u + 2u);
    const float3 e1 = b.p - a.p;
    const float3 e2 = c.p - a.p;
    const float3 cr = cross(e1, e2);
    const float len2 = dot(cr, cr);
    if (len2 < 1e-12) {
        return false;
    }
    N = cr * rsqrt(len2);
    const float3 vN = a.n + b.n + c.n;
    if (dot(N, vN) < 0.0) {
        N = -N;
    }
    const float3 q = center - N * dot(N, center - a.p);
    const float d00 = dot(e1, e1);
    const float d01 = dot(e1, e2);
    const float d11 = dot(e2, e2);
    const float3 rel = q - a.p;
    const float denom = d00 * d11 - d01 * d01;
    if (abs(denom) < 1e-12) {
        return false;
    }
    const float v = (d11 * dot(rel, e1) - d01 * dot(rel, e2)) / denom;
    const float w = (d00 * dot(rel, e2) - d01 * dot(rel, e1)) / denom;
    const float u = 1.0 - v - w;
    if (u < -0.02 || v < -0.02 || w < -0.02) {
        return false;
    }
    const float su = saturate(u);
    const float sv = saturate(v);
    const float sw = saturate(w);
    const float sum = max(su + sv + sw, 1e-6);
    P = a.p * (su / sum) + b.p * (sv / sum) + c.p * (sw / sum);
    dist = length(center - P);
    return dist <= limit;
}
