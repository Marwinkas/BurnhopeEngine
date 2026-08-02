#pragma once
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../Render/Light.hpp"
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"
namespace burnhope
{
    inline uint64_t generateRandomID()
    {
        static std::random_device rd;
        static std::mt19937_64 eng(rd());
        static std::uniform_int_distribution<uint64_t> dist(1);
        return dist(eng);
    }

    struct IDComponent
    {
        uint64_t ID;

        IDComponent() : ID(generateRandomID()) {}
        IDComponent(uint64_t id) : ID(id) {}
    };

    class Transform
    {
    public:
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
        glm::mat4 matrix = glm::mat4(1.0f);
        bool updatematrix = true;

        [[nodiscard]] glm::mat4 rotationMatrix() const noexcept
        {
            glm::mat4 rot = glm::mat4(1.0f);
            rot = glm::rotate(rot, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rot = glm::rotate(rot, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            return rot;
        }

        [[nodiscard]] glm::vec3 rotateVector(glm::vec3 localDir) const noexcept
        {
            return glm::normalize(glm::vec3(rotationMatrix() * glm::vec4(localDir, 0.0f)));
        }

        void updateMatrixIfNeeded()
        {
            if (updatematrix)
            {
                matrix = glm::translate(glm::mat4(1.0f), position)
                       * rotationMatrix()
                       * glm::scale(glm::mat4(1.0f), scale);
                updatematrix = false;
            }
        }
    };
    struct TagComponent
    {
        std::string name = "Entity";
        std::vector<std::string> tags;
        std::vector<std::string> layers;
        bool isPhantom = false;
    };
    struct TransformComponent
    {
        Transform transform;
    };
    struct MeshComponent
    {
        std::string modelPath = "";
        std::vector<std::string> materialPaths;
        std::shared_ptr<BurnhopeModel> model;
        std::vector<std::shared_ptr<Material>> materials;
        std::string skeletonPath = "";
        std::string animationPath = "";
        bool isStatic = false;
        bool isVisible = true;
        bool castShadow = true;
        float animationTime = 0.0f;
    };
    struct LightComponent
    {
        Light light;
        bool needsShadowUpdate = true;
    };
    struct HierarchyComponent
    {
        uint64_t parentID = 0;
        std::vector<uint64_t> childrenIDs;
    };

    struct ReflectionProbeComponent {
        float radius = 10.0f;
        int resolution = 256;
        bool updateNeeded = true;
        int textureIndex = -1; 
    };
    
    struct DecalComponent {
        std::string albedoPath = "";
        std::string normalPath = "";
        std::shared_ptr<BurnhopeTexture> albedoTex;
        std::shared_ptr<BurnhopeTexture> normalTex;
        float opacity = 1.0f;
        int albedoTexIdx = -1;
        int normalTexIdx = -1;
    };
    struct PortalComponent {
    entt::entity targetPortal = entt::null;

    static glm::mat4 getPortalTransform(const glm::mat4& srcMatrix, const glm::mat4& dstMatrix) {
        glm::mat4 flipY = glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0, 1, 0));
        return dstMatrix * flipY * glm::inverse(srcMatrix);
    }
};
}