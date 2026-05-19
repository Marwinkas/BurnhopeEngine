#pragma once
#include "../Utils/Device.hpp"
#include "Texture.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/Components.hpp"
#include "Camera.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <array>
#include <memory>
#include <vector>
#include "../Utils/FrameInfo.hpp"

namespace burnhope
{
    struct LightGPUData
    {
        float4 posType;
        float4 colorInt;
        float4 dirRadius;
        float4 shadowParams;
        float4x4 lightSpaceMatrix;
    };
    struct PointFaceMatrices
    {
        float4x4 faces[6];
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
        BurnhopeTexture *getTexture() { return atlasTexture.get(); }
        void setTileViewport(VkCommandBuffer cmd, int pixelX, int pixelY, int tileSize) const;

    private:
        void createResources();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> atlasTexture;
    };

    // Виртуальные теневые карты (VSM) для оптимизации памяти
    class VirtualShadowMap
    {
    public:
        static constexpr uint32_t PAGE_SIZE = 128;
        static constexpr uint32_t MAX_PHYSICAL_PAGES = 1024; // 32x32 страницы = 4096x4096 физ. памяти
        static constexpr uint32_t VIRTUAL_PAGES_X = 4096; // Поддержка сотен локальных светильников
        static constexpr uint32_t VIRTUAL_PAGES_Y = 4096;

        VirtualShadowMap(BurnhopeDevice &device);
        ~VirtualShadowMap();

        BurnhopeTexture *getPhysicalAtlas() { return physicalAtlas.get(); }
        BurnhopeBuffer *getPageTable() { return pageTableBuffer.get(); }
        BurnhopeBuffer *getAllocator() { return physicalPageAllocator.get(); }
    private:
        void createResources();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> physicalAtlas;
        std::unique_ptr<BurnhopeBuffer> pageTableBuffer;
        std::unique_ptr<BurnhopeBuffer> physicalPageAllocator;
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
        VkImageView getCascadeView(int cascade) const { return cascadeViews[cascade]; }
        BurnhopeTexture *getTexture() { return csmTexture.get(); }
        std::array<float4x4, CASCADE_COUNT> calculateMatrices(
            const Camera &camera,
            float3 sunDir,
            const std::array<float, CASCADE_COUNT> &splits) const;

    private:
        float4x4 calculateCascadeMatrix(float nearP, float farP,
                                         const Camera &camera, float3 sunDir, float shadowSize) const;
        void createResources();
        BurnhopeDevice &device;
        std::unique_ptr<BurnhopeTexture> csmTexture;
        std::array<VkImageView, CASCADE_COUNT> cascadeViews{};
    };
    class BurnhopeShadowSystem
    {
    public:
        BurnhopeShadowSystem(BurnhopeDevice &device);
        ~BurnhopeShadowSystem() = default;
        BurnhopeShadowSystem(const BurnhopeShadowSystem &) = delete;
        BurnhopeShadowSystem &operator=(const BurnhopeShadowSystem &) = delete;
        void updateLights(flecs::world &registry, const float3 &camPos);
        const LightUBOData &getLightUBO() const { return lightUBO; }
        const std::array<float4x4, BurnhopeCSM::CASCADE_COUNT> &getCascadeMatrices() const
        {
            return cascadeMatrices;
        }
        PointFaceMatrices faceMatricesData[100]{};
        const PointFaceMatrices *getFaceMatricesData() const { return faceMatricesData; }
        float3 getSunDir() const { return sunDir; }
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
        std::array<float4x4, BurnhopeCSM::CASCADE_COUNT> cascadeMatrices{};
        float3 sunDir{0.0f, -1.0f, 0.0f};
    };
}
