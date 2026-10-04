#pragma once

#include "gpu_scene/Camera.hpp"
#include "gpu_scene/InstanceData.hpp"

#include <cstdint>

#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace burnhope {

struct InstanceBounds {
    float center[3]{};
    float radius = 0.0f;
};

#if defined(__SSE2__)
inline uint32_t sphereCulledMask(const CameraCull& cam, const float* x, const float* y, const float* z, const float* radius) {
    const __m128 px = _mm_loadu_ps(x);
    const __m128 py = _mm_loadu_ps(y);
    const __m128 pz = _mm_loadu_ps(z);
    const __m128 pr = _mm_loadu_ps(radius);
    const __m128 zero = _mm_setzero_ps();
    __m128 dead = zero;
    for (int i = 0; i < 6; ++i) {
        const __m128 d = _mm_add_ps(
            _mm_add_ps(_mm_mul_ps(px, _mm_set1_ps(cam.planes[i][0])), _mm_mul_ps(py, _mm_set1_ps(cam.planes[i][1]))),
            _mm_add_ps(_mm_mul_ps(pz, _mm_set1_ps(cam.planes[i][2])), _mm_set1_ps(cam.planes[i][3])));
        dead = _mm_or_ps(dead, _mm_cmplt_ps(d, _mm_sub_ps(zero, pr)));
    }
    return static_cast<uint32_t>(_mm_movemask_ps(dead));
}
#endif

inline bool instanceLod0(const GpuInstance& inst, bool lod0Only) {
    return !lod0Only || inst.pad[0] == 0u;
}

inline uint32_t cullInstances(
    const CameraCull& cam,
    const GpuInstance* src,
    uint32_t count,
    uint32_t* visible,
    uint32_t visibleCap,
    bool lod0Only = false) {
    if (src == nullptr || visible == nullptr || visibleCap == 0) {
        return 0;
    }
    uint32_t n = 0;
    uint32_t i = 0;
#if defined(__SSE2__)
    for (; i + 4u <= count && n < visibleCap; i += 4u) {
        float x[4], y[4], z[4], r[4];
        uint8_t keep[4];
        for (uint32_t lane = 0; lane < 4u; ++lane) {
            const GpuInstance& inst = src[i + lane];
            x[lane] = inst.sphere[0];
            y[lane] = inst.sphere[1];
            z[lane] = inst.sphere[2];
            r[lane] = inst.sphere[3];
            keep[lane] = instanceLod0(inst, lod0Only) ? 1u : 0u;
        }
        const uint32_t culled = sphereCulledMask(cam, x, y, z, r);
        for (uint32_t lane = 0; lane < 4u && n < visibleCap; ++lane) {
            if (keep[lane] != 0u && (culled & (1u << lane)) == 0u) {
                visible[n++] = i + lane;
            }
        }
    }
#endif
    for (; i < count && n < visibleCap; ++i) {
        const GpuInstance& inst = src[i];
        if (!instanceLod0(inst, lod0Only)) {
            continue;
        }
        if (cameraSphereVisible(cam, inst.sphere[0], inst.sphere[1], inst.sphere[2], inst.sphere[3])) {
            visible[n++] = i;
        }
    }
    return n;
}

inline uint32_t cullInstances(const CameraCull& cam, const InstanceBounds* bounds, uint32_t count, uint32_t* outVisibleIndices, uint32_t visibleCap) {
    if (bounds == nullptr || outVisibleIndices == nullptr || visibleCap == 0) {
        return 0;
    }
    uint32_t n = 0;
    uint32_t i = 0;
#if defined(__SSE2__)
    for (; i + 4u <= count && n < visibleCap; i += 4u) {
        float x[4], y[4], z[4], r[4];
        for (uint32_t lane = 0; lane < 4u; ++lane) {
            x[lane] = bounds[i + lane].center[0];
            y[lane] = bounds[i + lane].center[1];
            z[lane] = bounds[i + lane].center[2];
            r[lane] = bounds[i + lane].radius;
        }
        const uint32_t culled = sphereCulledMask(cam, x, y, z, r);
        for (uint32_t lane = 0; lane < 4u && n < visibleCap; ++lane) {
            if ((culled & (1u << lane)) == 0u) {
                outVisibleIndices[n++] = i + lane;
            }
        }
    }
#endif
    for (; i < count && n < visibleCap; ++i) {
        const InstanceBounds& b = bounds[i];
        if (cameraSphereVisible(cam, b.center[0], b.center[1], b.center[2], b.radius)) {
            outVisibleIndices[n++] = i;
        }
    }
    return n;
}

} // namespace burnhope
