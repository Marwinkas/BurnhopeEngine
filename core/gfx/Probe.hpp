#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kProbeGrid = 4;
constexpr uint32_t kProbeCount = kProbeGrid * kProbeGrid * kProbeGrid;

// Nine L2 bands, RGB interleaved. Band 0 is the constant term.
struct alignas(16) ProbeGpu {
    float sh[27]{};
    float pad[5]{};
};

static_assert(std::is_trivially_copyable_v<ProbeGpu>);
static_assert(sizeof(ProbeGpu) == 128);

inline void probeSetGray(ProbeGpu& probe, float gray) {
    std::memset(&probe, 0, sizeof(probe));
    const float dc = gray / 0.2820948f;
    probe.sh[0] = dc;
    probe.sh[1] = dc;
    probe.sh[2] = dc;
}

inline void probeEval(const ProbeGpu& probe, float nx, float ny, float nz, float out[3]) {
    const float b0 = 0.2820948f;
    const float b1 = 0.4886025f;
    const float b2 = 1.0925484f;
    const float b3 = 0.3153916f;
    const float b4 = 0.5462742f;
    const float basis[9] = {
        b0,
        b1 * ny,
        b1 * nz,
        b1 * nx,
        b2 * nx * ny,
        b2 * ny * nz,
        b3 * (3.0f * nz * nz - 1.0f),
        b2 * nx * nz,
        b4 * (nx * nx - ny * ny),
    };
    out[0] = out[1] = out[2] = 0.0f;
    for (uint32_t band = 0; band < 9; ++band) {
        out[0] += probe.sh[band * 3 + 0] * basis[band];
        out[1] += probe.sh[band * 3 + 1] * basis[band];
        out[2] += probe.sh[band * 3 + 2] * basis[band];
    }
}

} // namespace burnhope
