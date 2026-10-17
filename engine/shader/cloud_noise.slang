[[vk::binding(46, 0)]]
RWStructuredBuffer<float> gNoise;

float3 cloudHash3(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

float worleyPeriodic(float3 uvw, float cells) {
    const float3 p = uvw * cells;
    const float3 cell = floor(p);
    const float3 f = p - cell;
    float best = 1.0;
    [unroll]
    for (int z = -1; z <= 1; ++z) {
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            [unroll]
            for (int x = -1; x <= 1; ++x) {
                const float3 offset = float3(float(x), float(y), float(z));
                float3 c = cell + offset;
                c = c - floor(c / cells) * cells;
                const float3 h = cloudHash3(c);
                const float3 r = offset + h - f;
                best = min(best, dot(r, r));
            }
        }
    }
    return 1.0 - saturate(sqrt(best));
}

float gradNoisePeriodic(float3 uvw, float cells) {
    const float3 p = uvw * cells;
    const float3 i = floor(p);
    const float3 f = p - i;
    const float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float acc = 0.0;
    [unroll]
    for (int dz = 0; dz <= 1; ++dz) {
        [unroll]
        for (int dy = 0; dy <= 1; ++dy) {
            [unroll]
            for (int dx = 0; dx <= 1; ++dx) {
                const float3 off = float3(float(dx), float(dy), float(dz));
                float3 c = i + off;
                c = c - floor(c / cells) * cells;
                const float3 g = normalize(cloudHash3(c) * 2.0 - 1.0);
                const float3 d = f - off;
                const float wx = dx == 0 ? (1.0 - u.x) : u.x;
                const float wy = dy == 0 ? (1.0 - u.y) : u.y;
                const float wz = dz == 0 ? (1.0 - u.z) : u.z;
                acc += dot(g, d) * wx * wy * wz;
            }
        }
    }
    return acc * 0.5 + 0.5;
}

float remap01(float v, float l0, float h0) {
    return saturate((v - l0) / max(h0 - l0, 1e-4));
}

[shader("compute")]
[numthreads(4, 4, 4)]
void cloudNoise(uint3 id : SV_DispatchThreadID) {
    const uint shape = 128u;
    const uint detail = 32u;
    if (id.x < shape && id.y < shape && id.z < shape) {
        const float3 uvw = (float3(id) + 0.5) / float(shape);
        const float perlin = gradNoisePeriodic(uvw, 4.0) * 0.55
                           + gradNoisePeriodic(uvw, 8.0) * 0.30
                           + gradNoisePeriodic(uvw, 16.0) * 0.15;
        const float w0 = worleyPeriodic(uvw, 6.0);
        const float w1 = worleyPeriodic(uvw, 12.0);
        const float w2 = worleyPeriodic(uvw, 24.0);
        const float worleyFbm = w0 * 0.625 + w1 * 0.25 + w2 * 0.125;
        const float billow = remap01(perlin, (1.0 - worleyFbm) * 0.65, 1.0);
        gNoise[id.x + id.y * shape + id.z * shape * shape] = billow;
    }
    if (id.x < detail && id.y < detail && id.z < detail) {
        const float3 duvw = (float3(id) + 0.5) / float(detail);
        const float dw0 = worleyPeriodic(duvw, 4.0);
        const float dw1 = worleyPeriodic(duvw, 8.0);
        const float dw2 = worleyPeriodic(duvw, 16.0);
        const uint base = shape * shape * shape;
        gNoise[base + id.x + id.y * detail + id.z * detail * detail] =
            saturate(dw0 * 0.625 + dw1 * 0.25 + dw2 * 0.125);
    }
}