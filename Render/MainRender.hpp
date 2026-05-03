#pragma once

#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/FrameInfo.hpp"
#include "../Utils/Components.hpp" // Твои компоненты (Transform, Mesh)
#include "CullingSystem.hpp" 
// libs
#include <entt/entt.hpp>
#include <memory>
#include <vector>

namespace burnhope {
    struct GBuffer {
        // 1. Normal (RGB16F) + Roughness (A)
        std::unique_ptr<BurnhopeTexture> gNormalRoughness;
        // 2. Albedo (RGBA8/16) + Metallic (A)
        std::unique_ptr<BurnhopeTexture> gAlbedoMetallic;
        // 3. Height (R) + AO (G) + Emission (B)
        std::unique_ptr<BurnhopeTexture> gExtra;
        // 4. Depth
        VkImage depthImage;
        VkDeviceMemory depthImageMemory;
        VkImageView depthImageView;
    };
    struct alignas(16) ObjectData {
        glm::mat4 modelMatrix;
        uint32_t materialID;
        uint32_t meshID;
        uint32_t padding1;
        uint32_t padding2;
    };

    struct alignas(16) MaterialData {
        int albedoIdx;
        int normalIdx;
        int heightIdx;
        int metallicIdx;
        int roughnessIdx;
        int aoIdx;
        int hasAlbedo, hasNormal, hasHeight, hasMetallic, hasRoughness, hasAO;
        int useTriplanar;
        float triplanarScale;
        glm::vec2 uvScale;
        int useORM; // <--- Наш новый переключатель
        int pad1, pad2, pad3; // Выравнивание до 16 байт (std430)
    };
    class GeometryRenderSystem {
    public:
        GeometryRenderSystem(BurnhopeDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
        ~GeometryRenderSystem() {}
        BurnhopeDescriptorSetLayout* getRenderSystemLayout() const { return renderSystemLayout.get(); }
        BurnhopeDescriptorSetLayout* getTextureLayout() const { return textureLayout.get(); }
        GeometryRenderSystem(const GeometryRenderSystem&) = delete;
        GeometryRenderSystem& operator=(const GeometryRenderSystem&) = delete;

        void renderEntities(FrameInfo& frameInfo, entt::registry& registry, VkDescriptorSet storageSet, VkDescriptorSet textureSet,
        CullingSystem& cullingSystem,   // ← добавляем
        uint32_t totalSubMeshCount);

    private:
        void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
        void createPipeline(VkRenderPass renderPass);

        BurnhopeDevice& lveDevice;

        std::unique_ptr<BurnhopePipeline> lvePipeline;
        VkPipelineLayout pipelineLayout;

        std::unique_ptr<BurnhopeDescriptorSetLayout> renderSystemLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> textureLayout;
    };
}