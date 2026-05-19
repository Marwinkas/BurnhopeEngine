#ifndef LIGHT_CLASS_H
#define LIGHT_CLASS_H
#include "../Utils/DirectXMathCompat.hpp"
#include <string>
namespace burnhope
{
    enum class LightType
    {
        Directional,
        Point,
        Spot,
        Rect,
        Sky,
        None
    };
    enum class LightMobility
    {
        Static,
        Movable
    };
    struct PointLightData
    {
        float4 posType;
        float4 colorInt;
        float4 dirRadius;
        float4 shadowParams;
        float4x4 lightSpaceMatrix;
    };
    struct LightUBOBlock
    {
        int activeLightsCount;
        int padding[3];
        PointLightData lights[100];
    };
    class Light
    {
    public:
        bool enable = false;
        LightType type = LightType::Point;
        LightMobility mobility = LightMobility::Movable;
        bool hasBakedShadows = false;
        float3 color = float3{1.0f, 1.0f, 1.0f};
        float intensity = 1.0f;
        float radius = 10.0f;
        float innerCone = 12.5f;
        float outerCone = 17.5f;
        bool castShadows = true;
        bool needsShadowUpdate = true;
        float4x4 lightSpaceMatrix = MatrixIdentity();
        int shadowSlot = -1;
        int shadowTileSize = 512;
    };
}
#endif