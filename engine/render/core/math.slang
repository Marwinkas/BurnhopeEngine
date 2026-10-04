static const float kPi = 3.14159265;
static const float kInvPi = 0.31830988618;

float luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float fastAcos(float x) {
    x = clamp(x, -1.0, 1.0);
    const float ax = abs(x);
    const float res = (-0.156583 * ax + 1.570796) * sqrt(max(1.0 - ax, 0.0));
    return x >= 0.0 ? res : 3.141593 - res;
}

float bayer4(uint2 p) {
    const float table[16] = {
        1.0 / 16.0, 9.0 / 16.0, 3.0 / 16.0, 11.0 / 16.0,
        13.0 / 16.0, 5.0 / 16.0, 15.0 / 16.0, 7.0 / 16.0,
        4.0 / 16.0, 12.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0,
        0.0 / 16.0, 8.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0,
    };
    return table[(p.x & 3u) + (p.y & 3u) * 4u];
}

float2 octEncode(float3 n) {
    const float3 a = abs(n);
    n.xy /= max(a.x + a.y + a.z, 1e-6);
    if (n.z < 0.0) {
        const float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return n.xy * 0.5 + 0.5;
}

float3 octDecode(float2 e) {
    const float2 f = e * 2.0 - 1.0;
    float3 n = float3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        const float2 s = float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * s;
    }
    return normalize(n);
}

void cubeDirToFaceUV(float3 dir, out uint face, out float2 uv) {
    const float3 a = abs(dir);
    if (a.x >= a.y && a.x >= a.z) {
        face = dir.x >= 0.0 ? 0u : 1u;
        uv = float2(dir.x >= 0.0 ? -dir.z : dir.z, -dir.y) / a.x;
    } else if (a.y >= a.z) {
        face = dir.y >= 0.0 ? 2u : 3u;
        uv = float2(dir.x, dir.y >= 0.0 ? dir.z : -dir.z) / a.y;
    } else {
        face = dir.z >= 0.0 ? 4u : 5u;
        uv = float2(dir.z >= 0.0 ? dir.x : -dir.x, -dir.y) / a.z;
    }
    uv = uv * 0.5 + 0.5;
}
