#pragma once
#include "../Utils/Device.hpp"
#include "Texture.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/Components.hpp"
#include "Camera.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include <array>
namespace burnhope
{
    struct LightGPUData
    {
        glm::vec4 posType;
        glm::vec4 colorInt;
        glm::vec4 dirRadius;
        glm::vec4 shadowParams;
        glm::mat4 lightSpaceMatrix;
    };
    struct PointFaceMatrices
    {
        glm::mat4 faces[6];
    };
    struct LightUBOData
    {
        int activeLightsCount;
        int _pad[3];
        LightGPUData lights[100];
    };
    class BurnhopeShadowAtlas
    {
    public:
        static constexpr int ATLAS_RESOLUTION = 4096;
        static constexpr int MIN_TILE = 128;
        static constexpr int ATLAS_IN_UNITS = ATLAS_RESOLUTION / MIN_TILE;
        BurnhopeShadowAtlas(BurnhopeDevice &device);
        ~BurnhopeShadowAtlas();
        BurnhopeShadowAtlas(const BurnhopeShadowAtlas &) = delete;
        BurnhopeShadowAtlas &operator=(const BurnhopeShadowAtlas &) = delete;
        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer() const { return framebuffer; }
        BurnhopeTexture *getTexture() { return atlasTexture.get(); }
        void setTileViewport(VkCommandBuffer cmd, int pixelX, int pixelY, int tileSize) const;

    private:
        void createResources();
        void createRenderPass();
        void createFramebuffer();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> atlasTexture;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    // Виртуальные теневые карты (VSM) для оптимизации памяти
    class VirtualShadowMap
    {
    public:
        static constexpr uint32_t PAGE_SIZE = 128;
        static constexpr uint32_t MAX_PHYSICAL_PAGES = 1024; // 32x32 страницы = 4096x4096 физ. памяти
        static constexpr uint32_t VIRTUAL_PAGES_X = 128; // Итоговое вирт. разрешение: 16384x16384
        static constexpr uint32_t VIRTUAL_PAGES_Y = 128;

        VirtualShadowMap(BurnhopeDevice &device);
        ~VirtualShadowMap();

        BurnhopeTexture *getPhysicalAtlas() { return physicalAtlas.get(); }
        BurnhopeBuffer *getPageTable() { return pageTableBuffer.get(); }
        BurnhopeBuffer *getAllocator() { return physicalPageAllocator.get(); }
        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer() const { return framebuffer; }
    private:
        void createResources();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> physicalAtlas;
        std::unique_ptr<BurnhopeBuffer> pageTableBuffer;
        std::unique_ptr<BurnhopeBuffer> physicalPageAllocator;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    class BurnhopeCSM
    {
    public:
        static constexpr int CASCADE_COUNT = 4;
        static constexpr int SHADOW_MAP_SIZE = 2048;
        BurnhopeCSM(BurnhopeDevice &device);
        ~BurnhopeCSM();
        BurnhopeCSM(const BurnhopeCSM &) = delete;
        BurnhopeCSM &operator=(const BurnhopeCSM &) = delete;
        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer(int cascade) const { return framebuffers[cascade]; }
        BurnhopeTexture *getTexture() { return csmTexture.get(); }
        std::array<glm::mat4, CASCADE_COUNT> calculateMatrices(
            const Camera &camera,
            glm::vec3 sunDir,
            const std::array<float, CASCADE_COUNT> &splits) const;

    private:
        glm::mat4 calculateCascadeMatrix(float nearP, float farP,
                                         const Camera &camera, glm::vec3 sunDir, float shadowSize) const;
        void createResources();
        void createRenderPass();
        void createFramebuffers();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> csmTexture;
        std::array<VkImageView, CASCADE_COUNT> cascadeViews{};
        std::array<VkFramebuffer, CASCADE_COUNT> framebuffers{};
        VkRenderPass renderPass = VK_NULL_HANDLE;
    };
    class BurnhopeShadowSystem
    {
    public:
        BurnhopeShadowSystem(BurnhopeDevice &device);
        ~BurnhopeShadowSystem() = default;
        BurnhopeShadowSystem(const BurnhopeShadowSystem &) = delete;
        BurnhopeShadowSystem &operator=(const BurnhopeShadowSystem &) = delete;
        void updateLights(entt::registry &registry, const glm::vec3 &camPos);
        const LightUBOData &getLightUBO() const { return lightUBO; }
        const std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT> &getCascadeMatrices() const
        {
            return cascadeMatrices;
        }
        PointFaceMatrices faceMatricesData[100]{};
        const PointFaceMatrices *getFaceMatricesData() const { return faceMatricesData; }
        glm::vec3 getSunDir() const { return sunDir; }
        BurnhopeShadowAtlas *getAtlas() { return shadowAtlas.get(); }
        BurnhopeCSM *getCSM() { return csm.get(); }
        VirtualShadowMap *getVSM() { return vsm.get(); }
        std::array<float, 4> cascadeSplits = {25.0f, 80.0f, 200.0f, 400.0f};

    private:
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeShadowAtlas> shadowAtlas;
        std::unique_ptr<BurnhopeCSM> csm;
        std::unique_ptr<VirtualShadowMap> vsm;
        LightUBOData lightUBO{};
        std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT> cascadeMatrices{};
        glm::vec3 sunDir{0.0f, -1.0f, 0.0f};
    };
}
