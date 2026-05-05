#pragma once
#include "../Utils/Buffer.hpp"
#include "../Utils/Device.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "Material.hpp"
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
namespace burnhope
{
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texUV;
        glm::vec3 tangent;
        glm::vec3 bitangent;
        static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
        static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
        bool operator==(const Vertex &other) const
        {
            return position == other.position &&
                   normal == other.normal &&
                   texUV == other.texUV &&
                   tangent == other.tangent &&
                   bitangent == other.bitangent;
        }
    };
    struct SubMesh
    {
        uint32_t lodCount;
        uint32_t indexCounts[4];
        uint32_t firstIndices[4];
        uint32_t materialIndex;
        glm::vec3 aabbMin = glm::vec3(0.0f);
        glm::vec3 aabbMax = glm::vec3(0.0f);
        float boundingRadius = 0.0f;
    };
    struct MaterialPaths
    {
        std::string albedo;
        std::string normal;
        std::string orm;
    };
    struct Builder
    {
        std::vector<Vertex> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<SubMesh> subMeshes{};
        std::vector<MaterialPaths> materialPaths;
        std::string modelDir;
        void loadModel(const std::string &filepath);
    };
    class BurnhopeModel
    {
    public:
        const std::vector<SubMesh> &getSubMeshes() const { return subMeshes; }
        BurnhopeModel(BurnhopeDevice &device, const Builder &builder);
        ~BurnhopeModel();
        BurnhopeModel(const BurnhopeModel &) = delete;
        BurnhopeModel &operator=(const BurnhopeModel &) = delete;
        static std::unique_ptr<BurnhopeModel> createModelFromFile(
            BurnhopeDevice &device, const std::string &filepath);
        void bind(VkCommandBuffer commandBuffer);
        void draw(VkCommandBuffer commandBuffer);
        std::unique_ptr<BurnhopeBuffer> vertexBuffer;
        uint32_t vertexCount;
        bool hasIndexBuffer = false;
        std::unique_ptr<BurnhopeBuffer> indexBuffer;
        uint32_t indexCount;
        std::vector<std::shared_ptr<Material>> materials;
        VkDeviceAddress getBLASAddress() const { return blasAddress; }
        // Добавь эти поля в private или protected
VkDeviceAddress getVertexBufferAddress() const { return vertexBufferAddress; }
    VkDeviceAddress getIndexBufferAddress() const { return indexBufferAddress; }
    // Добавь этот метод в public
    void createBLAS();
    VkAccelerationStructureKHR getBLAS() const { return blasHandle; }
    private:
        std::vector<SubMesh> subMeshes;
        void createVertexBuffers(const std::vector<Vertex> &vertices);
        void createIndexBuffers(const std::vector<uint32_t> &indices);
        BurnhopeDevice &lveDevice;
        std::unique_ptr<BurnhopeBuffer> blasBuffer;
        VkAccelerationStructureKHR blasHandle = VK_NULL_HANDLE;
        VkDeviceAddress blasAddress = 0;
VkDeviceAddress vertexBufferAddress = 0;
    VkDeviceAddress indexBufferAddress = 0;
    };
}