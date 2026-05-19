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
    float4x4 modelMatrix;       // 0
    uint32_t materialID;         // 64
    uint32_t indexCount;         // 68
    uint32_t vrsRate;            // 72
    uint32_t boneOffset;         // 76
    uint64_t posBufferAddress;   // 80 (Perfectly aligned to 8)
    uint64_t attrBufferAddress;  // 88
    uint64_t indexBufferAddress; // 96
    uint64_t colorBufferAddress; // 104
    uint64_t uv2BufferAddress;   // 112
    uint64_t animBufferAddress;  // 120
    float4 aabbMin;           // 128 (Perfectly aligned to 16)
    float4 aabbMax;           // 144
}; // Total: 160 bytes.

 struct alignas(16) MaterialData
{
    int albedoAlphaIdx; int normalIdx; int ormxIdx; int emissiveIdx;
    int useTriplanar; int isTransparent; int repeatTexture; int pad1;
    alignas(8) float2 uvScale; float triplanarScale; float emissiveIntensity;
    alignas(16) float4 albedoColor;
    alignas(16) float4 emissiveColor;
    float metallicStrength; float roughnessStrength; float normalStrength; float heightStrength;
    float aoStrength; float pad2; float pad3; float pad4;
};
static_assert(sizeof(MaterialData) % 16 == 0, "MaterialData size must be a multiple of 16");
    class GeometryRenderSystem
    {
    public:
        GeometryRenderSystem(BurnhopeDevice &device, VkDescriptorSetLayout globalSetLayout);
        ~GeometryRenderSystem() {}
        BurnhopeDescriptorSetLayout *getRenderSystemLayout() const { return renderSystemLayout.get(); }
        BurnhopeDescriptorSetLayout *getTextureLayout() const { return textureLayout.get(); }
        GeometryRenderSystem(const GeometryRenderSystem &) = delete;
        GeometryRenderSystem &operator=(const GeometryRenderSystem &) = delete;
        void renderEntities(
            FrameInfo &frameInfo,
            flecs::world &registry,
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