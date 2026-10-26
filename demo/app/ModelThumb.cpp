#include "app/ModelThumb.hpp"

#include "asset/ModelMesh.hpp"
#include "files/FileThumb.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace burnhope {

namespace {

constexpr uint32_t kSuper = 2;
constexpr uint32_t kSide = kThumbPx * kSuper;
constexpr uint32_t kAtlasSide = kThumbCols * kThumbPx;

struct Frame {
    float center[3]{};
    float scale = 1.0f;
    float rot[9]{};
};

// Rotation of the preview camera: yaw about Y, then pitch about X.
Frame frameOf(const ModelMesh& mesh) {
    float lo[3] = {1e30f, 1e30f, 1e30f};
    float hi[3] = {-1e30f, -1e30f, -1e30f};
    for (size_t i = 0; i + 2 < mesh.xyz.size(); i += 3) {
        for (size_t c = 0; c < 3; ++c) {
            lo[c] = std::min(lo[c], mesh.xyz[i + c]);
            hi[c] = std::max(hi[c], mesh.xyz[i + c]);
        }
    }
    Frame f{};
    float radius = 1e-6f;
    for (size_t c = 0; c < 3; ++c) {
        f.center[c] = (lo[c] + hi[c]) * 0.5f;
        const float half = (hi[c] - lo[c]) * 0.5f;
        radius += half * half;
    }
    f.scale = 0.46f * static_cast<float>(kSide) / std::sqrt(radius);
    const float yaw = 0.6f;
    const float pitch = 0.45f;
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const float m[9] = {
        cy, 0.0f, sy,
        sy * sp, cp, -cy * sp,
        -sy * cp, sp, cy * cp,
    };
    std::copy(m, m + 9, f.rot);
    return f;
}

void project(const Frame& f, const float* p, float out[3]) {
    const float x = p[0] - f.center[0];
    const float y = p[1] - f.center[1];
    const float z = p[2] - f.center[2];
    out[0] = (f.rot[0] * x + f.rot[1] * y + f.rot[2] * z) * f.scale + static_cast<float>(kSide) * 0.5f;
    out[1] = static_cast<float>(kSide) * 0.5f - (f.rot[3] * x + f.rot[4] * y + f.rot[5] * z) * f.scale;
    out[2] = f.rot[6] * x + f.rot[7] * y + f.rot[8] * z;
}

} // namespace

bool modelThumbBake(uint8_t* atlas, uint8_t cell, const char* path) {
    ModelMesh mesh;
    if (atlas == nullptr || cell >= kThumbCells || !modelLoadMesh(path, mesh)) {
        return false;
    }
    const Frame frame = frameOf(mesh);
    std::vector<float> depth(static_cast<size_t>(kSide) * kSide, 0.0f);
    std::vector<uint8_t> shade(static_cast<size_t>(kSide) * kSide, 0);
    std::vector<uint8_t> hit(static_cast<size_t>(kSide) * kSide, 0);
    const float light[3] = {-0.4f, 0.7f, 0.6f};
    const float lightLen = std::sqrt(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    for (size_t t = 0; t + 2 < mesh.idx.size(); t += 3) {
        float v[3][3];
        for (size_t k = 0; k < 3; ++k) {
            project(frame, &mesh.xyz[static_cast<size_t>(mesh.idx[t + k]) * 3], v[k]);
        }
        const float area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[2][0] - v[0][0]) * (v[1][1] - v[0][1]);
        if (std::fabs(area) < 1e-6f) {
            continue;
        }
        // Face normal in camera space (x right, y up, z toward the viewer), lit by a fixed key light plus ambient.
        const float e1[3] = {v[1][0] - v[0][0], -(v[1][1] - v[0][1]), v[1][2] - v[0][2]};
        const float e2[3] = {v[2][0] - v[0][0], -(v[2][1] - v[0][1]), v[2][2] - v[0][2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (nl < 1e-12f) {
            continue;
        }
        const float facing = n[2] < 0.0f ? -1.0f : 1.0f;
        const float lambert = std::fabs((n[0] * light[0] + n[1] * light[1] + n[2] * light[2]) / (nl * lightLen) * facing);
        const uint8_t lum = static_cast<uint8_t>(std::min(1.0f, 0.28f + 0.72f * lambert) * 255.0f);
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({v[0][0], v[1][0], v[2][0]}))));
        const int x1 = std::min(static_cast<int>(kSide) - 1, static_cast<int>(std::ceil(std::max({v[0][0], v[1][0], v[2][0]}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({v[0][1], v[1][1], v[2][1]}))));
        const int y1 = std::min(static_cast<int>(kSide) - 1, static_cast<int>(std::ceil(std::max({v[0][1], v[1][1], v[2][1]}))));
        const float inv = 1.0f / area;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = ((v[1][0] - px) * (v[2][1] - py) - (v[2][0] - px) * (v[1][1] - py)) * inv;
                const float w1 = ((v[2][0] - px) * (v[0][1] - py) - (v[0][0] - px) * (v[2][1] - py)) * inv;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
                    continue;
                }
                const float z = w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2];
                const size_t at = static_cast<size_t>(y) * kSide + static_cast<size_t>(x);
                if (hit[at] == 0 || z > depth[at]) {
                    depth[at] = z;
                    shade[at] = lum;
                    hit[at] = 1;
                }
            }
        }
    }
    const uint32_t ox = (cell % kThumbCols) * kThumbPx;
    const uint32_t oy = (cell / kThumbCols) * kThumbPx;
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        for (uint32_t x = 0; x < kThumbPx; ++x) {
            uint32_t sum = 0;
            uint32_t covered = 0;
            for (uint32_t sy = 0; sy < kSuper; ++sy) {
                for (uint32_t sx = 0; sx < kSuper; ++sx) {
                    const size_t at = static_cast<size_t>(y * kSuper + sy) * kSide + x * kSuper + sx;
                    sum += hit[at] != 0 ? shade[at] : 0u;
                    covered += hit[at];
                }
            }
            uint8_t* dst = atlas + (static_cast<size_t>(oy + y) * kAtlasSide + ox + x) * 4u;
            const uint8_t lum = covered != 0 ? static_cast<uint8_t>(sum / covered) : uint8_t{0};
            // Cool grey-blue clay: luminance tints a fixed base colour.
            dst[0] = static_cast<uint8_t>(lum * 200u / 255u);
            dst[1] = static_cast<uint8_t>(lum * 214u / 255u);
            dst[2] = lum;
            dst[3] = static_cast<uint8_t>(covered * 255u / (kSuper * kSuper));
        }
    }
    return true;
}

} // namespace burnhope
