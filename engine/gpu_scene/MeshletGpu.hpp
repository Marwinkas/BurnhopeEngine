#pragma once

#include "gfx/Meshlet.hpp"

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct GpuMeshlet {
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    float coneAxis[3]{};
    float coneCutoff = -1.0f;
    float groupCenter[3]{};
    float groupRadius = 0.0f;
};

static_assert(sizeof(GpuMeshlet) == 48);
static_assert(sizeof(GpuMeshlet) % 16 == 0);

} // namespace burnhope
