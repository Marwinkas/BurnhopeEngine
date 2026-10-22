#pragma once

#include <cstdint>

namespace burnhope {

struct AudioWorld;

struct AudioListener {
    float origin[3]{};
    float ahead[3]{};
    float up[3]{0.0f, 1.0f, 0.0f};
};

struct AudioMesh {
    const float* xyz = nullptr;
    uint32_t vertexCount = 0;
    const int32_t* indices = nullptr;
    const int32_t* materialIndex = nullptr;
    uint32_t triangleCount = 0;
};

[[nodiscard]] AudioWorld* audioCreate();
void audioDestroy(AudioWorld* world);
bool audioSetMesh(AudioWorld* world, const AudioMesh& mesh);
void audioSetListener(AudioWorld* world, const AudioListener& ear);
void audioSetRain(AudioWorld* world, float openSky, float amount);
void audioTick(AudioWorld* world, float dt);

} // namespace burnhope
