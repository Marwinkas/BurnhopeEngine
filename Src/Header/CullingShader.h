#ifndef CULLINGSHADER_CLASS_H
#define CULLINGSHADER_CLASS_H
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>
#include <vector>
#include <string>
#include <array>
#include "shaderClass.h"
#include "Window.h"
#include "Camera.h"
struct BoundingSphere {
    glm::vec4 posAndRadius;
};
struct DrawCommand {
    unsigned int count;
    unsigned int instanceCount;
    unsigned int firstIndex;
    unsigned int baseVertex;
    unsigned int baseInstance;
};
struct Plane {
    glm::vec3 normal;
    float distance;
    void Normalize() {
        float mag = glm::length(normal);
        normal /= mag;
        distance /= mag;
    }
};
class ClusterGridShader : public Shader {
public:
    struct {
        GLint zFar, gridDimX, zNear, gridDimY, gridDimZ, projection;
    } loc;
    ClusterGridShader() : Shader("Shaders/cluster_grid.comp") {
        Init();
    }
    void Init() override {
        loc.zFar = GetLoc("zFar");
        loc.gridDimX = GetLoc("gridDimX");
        loc.zNear = GetLoc("zNear");
        loc.gridDimY = GetLoc("gridDimY");
        loc.gridDimZ = GetLoc("gridDimZ");
        loc.projection = GetLoc("projection");
    }
};
class LightCullingShader : public Shader {
public:
    LightCullingShader() : Shader("Shaders/light_culling.comp") {
        Init();
    }
    void Init() override {

    }
};
class CullingShader : public Shader {
public:
    Shader csmCullingShader;
    struct {
        GLint isShadowPass, camPos, lodDistances,objectCount, viewProjection, screenSize, hiZTexture;
        GLint lodCount[3];
        GLint lodOffset[3];
        GLint frustumPlanes[6];
    } loc;
    CullingShader() : Shader("Shaders/culling.comp") {
        Init();
        ssboObjects = GLBuffer(GL_SHADER_STORAGE_BUFFER, sizeof(BoundingSphere), nullptr, GL_DYNAMIC_DRAW, 0);
        ssboCommands = GLBuffer(GL_SHADER_STORAGE_BUFFER, sizeof(DrawCommand), nullptr, GL_DYNAMIC_DRAW, 1);
    }
    void Init() override {
        loc.camPos = GetLoc("camPos");
        loc.isShadowPass = GetLoc("isShadowPass");
        loc.lodDistances = GetLoc("lodDistances");
        for (int i = 0; i < 3; i++) {
            std::string countName = "meshLODs[" + std::to_string(i) + "].count";
            std::string offsetName = "meshLODs[" + std::to_string(i) + "].firstIndex";

            loc.lodCount[i] = GetLoc(countName.c_str());
            loc.lodOffset[i] = GetLoc(offsetName.c_str());
        }
        for (int i = 0; i < 6; i++) {
            std::string name = "frustumPlanes[" + std::to_string(i) + "]";
            loc.frustumPlanes[i] = GetLoc(name.c_str());
        }
        loc.objectCount = GetLoc("objectCount");
        loc.viewProjection = GetLoc("viewProjection");
        loc.screenSize = GetLoc("screenSize");
        loc.hiZTexture = GetLoc("hiZTexture");
        csmCullingShader = Shader("Shaders/csm_culling.comp");

    }
    LightCullingShader lightCullingShader;
    ClusterGridShader clusterGridShader;
    GLBuffer ssboObjects, ssboCommands;     
    std::array<Plane, 6> ExtractFrustumPlanes(const glm::mat4& vpMatrix) {
        std::array<Plane, 6> planes;
        planes[0].normal.x = vpMatrix[0][3] + vpMatrix[0][0]; planes[0].normal.y = vpMatrix[1][3] + vpMatrix[1][0]; planes[0].normal.z = vpMatrix[2][3] + vpMatrix[2][0]; planes[0].distance = vpMatrix[3][3] + vpMatrix[3][0];
        planes[1].normal.x = vpMatrix[0][3] - vpMatrix[0][0]; planes[1].normal.y = vpMatrix[1][3] - vpMatrix[1][0]; planes[1].normal.z = vpMatrix[2][3] - vpMatrix[2][0]; planes[1].distance = vpMatrix[3][3] - vpMatrix[3][0];
        planes[2].normal.x = vpMatrix[0][3] + vpMatrix[0][1]; planes[2].normal.y = vpMatrix[1][3] + vpMatrix[1][1]; planes[2].normal.z = vpMatrix[2][3] + vpMatrix[2][1]; planes[2].distance = vpMatrix[3][3] + vpMatrix[3][1];
        planes[3].normal.x = vpMatrix[0][3] - vpMatrix[0][1]; planes[3].normal.y = vpMatrix[1][3] - vpMatrix[1][1]; planes[3].normal.z = vpMatrix[2][3] - vpMatrix[2][1]; planes[3].distance = vpMatrix[3][3] - vpMatrix[3][1];
        planes[4].normal.x = vpMatrix[0][3] + vpMatrix[0][2]; planes[4].normal.y = vpMatrix[1][3] + vpMatrix[1][2]; planes[4].normal.z = vpMatrix[2][3] + vpMatrix[2][2]; planes[4].distance = vpMatrix[3][3] + vpMatrix[3][2];
        planes[5].normal.x = vpMatrix[0][3] - vpMatrix[0][2]; planes[5].normal.y = vpMatrix[1][3] - vpMatrix[1][2]; planes[5].normal.z = vpMatrix[2][3] - vpMatrix[2][2]; planes[5].distance = vpMatrix[3][3] - vpMatrix[3][2];
        for (int i = 0; i < 6; i++) planes[i].Normalize();
        return planes;
    }
};
#endif