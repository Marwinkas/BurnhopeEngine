#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/FrameInfo.hpp"
#include "../Utils/Components.hpp"
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
          VmaAllocation depthImageMemory;
        VkImageView depthImageView;
    };
struct alignas(16) ObjectData {
    glm::mat4 modelMatrix;   // 64 байта
    uint32_t materialID;     // 4 байта
    uint32_t indexCount;     // 4 байта
    uint32_t pad0[2];        // 8 байт
    uint64_t vertexBufferAddress; // 8 байт
    uint64_t indexBufferAddress;  // 8 байт
    glm::vec4 aabbMin;       // 16 байт
    glm::vec4 aabbMax;       // 16 байт
}; // Итого: 128 байт. Идеальное выравнивание.
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
            uint32_t totalSubMeshCount,
            bool useStencil,
            uint32_t vrsMode = 0,
            bool isZPrepass = false
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
          std::unique_ptr<GraphicsShader> zPrepassShader;
        std::unique_ptr<GraphicsShader> zPrepassPortalShader;
    };
}