#pragma once

#include "../Utils/Buffer.hpp"
#include "../Utils/Device.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "Material.hpp"
// std
#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <meshoptimizer.h>
namespace burnhope {
    struct Vertex {
        glm::vec3 position;   // 12 байт
        glm::vec3 normal;     // 12 байт  
        glm::vec2 texUV;      // 8 байт
        glm::vec3 tangent;    // 12 байт
        glm::vec3 bitangent;  // 12 байт

        static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

        bool operator==(const Vertex& other) const {
            return position == other.position &&
                normal == other.normal &&
                texUV == other.texUV &&
                tangent == other.tangent &&
                bitangent == other.bitangent;
        }
    };
    struct SubMesh {
    uint32_t lodCount;          // Сколько уровней есть
    uint32_t indexCounts[4];    // Количество индексов для каждого LOD
    uint32_t firstIndices[4];   // Смещение индексов для каждого LOD
    
        uint32_t materialIndex; // Индекс из исходного файла (FBX/OBJ)
        glm::vec3 aabbMin        = glm::vec3(0.0f);
        glm::vec3 aabbMax        = glm::vec3(0.0f);
        float     boundingRadius = 0.0f;
    };
    struct MaterialPaths {
    std::string albedo;
    std::string normal;
    std::string orm;
};

    struct Builder {
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<SubMesh> subMeshes{};

        std::vector<MaterialPaths> materialPaths;
        std::string modelDir; // Папка, где лежит модель (чтобы движок знал, где искать текстуры)

        void loadModel(const std::string& filepath);
    };
   
    class BurnhopeModel {
    public:
        std::vector<std::shared_ptr<Material>> loadedMaterials;
        const std::vector<SubMesh>& getSubMeshes() const { return subMeshes; }
        BurnhopeModel(BurnhopeDevice& device, const Builder& builder);
        ~BurnhopeModel();

        BurnhopeModel(const BurnhopeModel&) = delete;
        BurnhopeModel& operator=(const BurnhopeModel&) = delete;

        static std::unique_ptr<BurnhopeModel> createModelFromFile(
            BurnhopeDevice& device, const std::string& filepath);

        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);
        std::unique_ptr<BurnhopeBuffer> vertexBuffer;
        uint32_t vertexCount;

        bool hasIndexBuffer = false;
        std::unique_ptr<BurnhopeBuffer> indexBuffer;
        uint32_t indexCount;
    private:
        std::vector<SubMesh> subMeshes; // Наш новый список частей
        void createVertexBuffers(const std::vector<Vertex>& vertices);
        void createIndexBuffers(const std::vector<uint32_t>& indices);

        BurnhopeDevice& lveDevice;


    };
}  // namespace burnhope