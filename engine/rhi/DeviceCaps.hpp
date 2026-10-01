#pragma once

#include <cstdint>

namespace burnhope {

struct DeviceCaps {
    bool shaderObject = false;
    bool presentWait = false;
    bool presentId = false;
    bool dynamicRendering = false;
    bool timelineSemaphore = false;
    bool bufferDeviceAddress = false;
    bool sync2 = false;
    bool meshShader = false;
    bool rayQuery = false;
    bool descriptorHeap = false;
    bool dgc = false;
    bool maintenance6 = false;
    uint32_t apiMajor = 0;
    uint32_t apiMinor = 0;
    uint32_t apiPatch = 0;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    uint32_t computeFamily = 0;
    char deviceName[256]{};
};

} // namespace burnhope
