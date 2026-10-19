// 176 байт. Совпадает с SharcGpuParams в render/Sharc.hpp. Скалярная раскладка.

struct SharcGpuParams {
    float camX;
    float camY;
    float camZ;
    float sceneScale;
    float prevX;
    float prevY;
    float prevZ;
    float levelBias;
    uint capacity;
    uint accumulationFrameNum;
    uint staleFrameNum;
    uint frameIndex;
    float radianceScale;
    float roughnessThreshold;
    uint downscale;
    uint bouncesMax;
    uint tlasLo;
    uint tlasHi;
    float giGain;
    uint lightCount;
    float4x4 prevViewProj;
    uint restirSlice;
    uint restirOn;
    float shadowBias;
    float shadowRange;
    float sunDisk;
    uint sunRays;
    uint padSun0;
    uint padSun1;
};
