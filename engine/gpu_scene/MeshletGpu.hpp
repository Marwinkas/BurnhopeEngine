#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

struct GpuVertex {
    float px, py, pz, pad0;
    float nx, ny, nz, pad1;
};

struct GpuMeshlet {
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleCount = 0;
    float coneAxis[3]{};
    float coneCutoff = -1.0f;
};

} // namespace burnhope
