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
    bool hostImageCopy = false;
    // Фаза 1.5: frame pacing/present-режим без пересборки свопчейна.
    bool swapchainMaintenance1 = false;
    // Фаза 5+: запись командных буферов в несколько потоков без ограничения на вложенность.
    bool nestedCommandBuffer = false;
    // Будущий 3D-шейдинг: debug visbuffer без отдельного буфера.
    bool depthClampControl = false;
    bool fragmentShaderBarycentric = false;
    // Северная звезда: обнаруживаемая опция под будущий ReSTIR-деноизер/нейро-апскейл. Не включается
    // в фичи устройства, пока нет потребителя — только флаг в логе caps.
    bool cooperativeMatrix = false;
    uint32_t apiMajor = 0;
    uint32_t apiMinor = 0;
    uint32_t apiPatch = 0;
    uint32_t graphicsFamily = 0;
    uint32_t presentFamily = 0;
    uint32_t computeFamily = 0;
    uint32_t transferFamily = 0;
    char deviceName[256]{};
};

} // namespace burnhope
