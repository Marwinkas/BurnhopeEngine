#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/FrameInfo.hpp"
#include "../Utils/Components.hpp"
#include "CullingSystem.hpp"
#include "GraphicsShader.hpp"
#include <entt/entt.hpp>
#include <memory>
#include <vector>
namespace burnhope
{
    struct GBuffer
    {
        std::unique_ptr<BurnhopeTexture> gNormalRoughness;
        std::unique_ptr<BurnhopeTexture> gAlbedoMetallic;
        std::unique_ptr<BurnhopeTexture> gExtra;
        VkImage depthImage;
        VkDeviceMemory depthImageMemory;
        VkImageView depthImageView;
    };
struct alignas(16) ObjectData {
    glm::mat4 modelMatrix;   // 64 байта
    uint32_t materialID;     // 4 байта
    uint32_t pad0;           // 4 байта
    uint64_t vertexBufferAddress; // 8 байт
    uint64_t indexBufferAddress;  // 8 байт
    uint64_t pad1;           // 8 БАЙТ ДОБАВИТЬ! Итого: 96 байт
};
 struct alignas(16) MaterialData
{
    int albedoAlphaIdx; int normalIdx; int ormxIdx; int emissiveIdx;
    int useTriplanar; int isTransparent; int repeatTexture; int pad1;
    alignas(8) glm::vec2 uvScale; float triplanarScale; float emissiveIntensity;
    alignas(16) glm::vec4 albedoColor;
    alignas(16) glm::vec4 emissiveColor;
    float metallicStrength; float roughnessStrength; float normalStrength; float heightStrength;
    float aoStrength; float pad2; float pad3; float pad4;
};
static_assert(sizeof(MaterialData) % 16 == 0, "MaterialData size must be a multiple of 16");
    class GeometryRenderSystem
    {
    public:
        GeometryRenderSystem(BurnhopeDevice &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
        ~GeometryRenderSystem() {}
        BurnhopeDescriptorSetLayout *getRenderSystemLayout() const { return renderSystemLayout.get(); }
        BurnhopeDescriptorSetLayout *getTextureLayout() const { return textureLayout.get(); }
        GeometryRenderSystem(const GeometryRenderSystem &) = delete;
        GeometryRenderSystem &operator=(const GeometryRenderSystem &) = delete;
        void renderEntities(
            FrameInfo &frameInfo,
            entt::registry &registry,
            VkDescriptorSet storageSet,
            VkDescriptorSet textureSet,
            CullingSystem &cullingSystem,
            uint32_t totalSubMeshCount,
            bool useStencil
        );

    private:
        void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
        void createPipeline(VkRenderPass renderPass);
        BurnhopeDevice &lveDevice;
        std::unique_ptr<BurnhopePipeline> lvePipeline;
        VkPipelineLayout pipelineLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> renderSystemLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> textureLayout;
        std::unique_ptr<GraphicsShader> shader;
        std::unique_ptr<GraphicsShader> portalShader;
    };
}