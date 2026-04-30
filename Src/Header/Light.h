#ifndef LIGHT_CLASS_H
#define LIGHT_CLASS_H
#include <glm/glm.hpp>
#include <string>
enum class LightType { Directional, Point, Spot, Rect, Sky,None };
enum class LightMobility { Static, Movable };   
struct PointLightData {
    glm::vec4 posType;
    glm::vec4 colorInt;
    glm::vec4 dirRadius;
    glm::vec4 shadowParams; 
    glm::mat4 lightSpaceMatrix; 
};
struct LightUBOBlock {
    int activeLightsCount;
    int padding[3]; 
    PointLightData lights[100];
};
class Light {
public:
    bool enable = false;
    LightType type = LightType::Point;
    LightMobility mobility = LightMobility::Movable;
    bool hasBakedShadows = false;
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
    float intensity = 1.0f;
    float radius = 10.0f;
    float innerCone = 12.5f;
    float outerCone = 17.5f;
    bool castShadows = true;
    bool needsShadowUpdate = true; 
    glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
    int shadowSlot = -1;
    int shadowTileSize = 512;
};
#endif