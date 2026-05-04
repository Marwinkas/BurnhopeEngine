#pragma once
#include <entt/entt.hpp>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../Render/Light.hpp"
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"
namespace burnhope
{
    class Transform
    {
    public:
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 rotation = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 scale = glm::vec3(1.0f, 1.0f, 1.0f);
        glm::mat4 matrix = glm::mat4(1.0f);
        bool updatematrix = true;
    };
    struct TagComponent
    {
        std::string name = "Entity";
        std::vector<std::string> tags;
        std::vector<std::string> layers;
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
        bool isStatic = false;
        bool isVisible = true;
        bool castShadow = true;
    };
    struct LightComponent
    {
        Light light;
    };
    struct HierarchyComponent
    {
        entt::entity parent = entt::null;
        std::vector<entt::entity> children;
    };
}