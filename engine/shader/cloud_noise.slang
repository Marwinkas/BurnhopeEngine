[[vk::binding(46, 0)]]
RWStructuredBuffer<float> gNoise;

float cloudHash(float x, float y, float z) {
    x = frac(x * 0.1031);
    y = frac(y * 0.1031);
    z = frac(z * 0.1031);
    const float d = x * (y + 33.33) + y * (z + 33.33) + z * (x + 33.33);
    x += d;
    y += d;
    z += d;
    return frac((x + y) * z);
}

float cloudWorley(float px, float py, float pz) {
    const float cx = floor(px);
    const float cy = floor(py);
    const float cz = floor(pz);
    const float fx = px - cx;
    const float fy = py - cy;
    const float fz = pz - cz;
    float best = 1.0;
    [unroll]
    for (int z = -1; z <= 1; ++z) {
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            [unroll]
            for (int x = -1; x <= 1; ++x) {
                const float hx = cloudHash(cx + float(x), cy + float(y), cz + float(z));
                const float rx = float(x) + hx - fx;
                const float ry = float(y) + hx - fy;
                const float rz = float(z) + hx - fz;
                best = min(best, rx * rx + ry * ry + rz * rz);
            }
        }
    }
    return sqrt(best);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void cloudNoise(uint3 id : SV_DispatchThreadID) {
    const uint shape = 128u;
    const uint detail = 32u;
    if (id.x < shape && id.y < shape && id.z < shape) {
        gNoise[id.x + id.y * shape + id.z * shape * shape] =
            cloudWorley(float(id.x) + 0.5, float(id.y) + 0.5, float(id.z) + 0.5);
    }
    if (id.x < detail && id.y < detail && id.z < detail) {
        const uint base = shape * shape * shape;
        gNoise[base + id.x + id.y * detail + id.z * detail * detail] =
            cloudWorley(float(id.x) + 0.5, float(id.y) + 0.5, float(id.z) + 0.5);
    }
}
