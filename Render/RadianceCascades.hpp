#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Descriptors.hpp"
#include "Texture.hpp"
#include "ComputeShader.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <array>
#include <memory>
#include <vector>
namespace burnhope
{
    struct RCConfig
    {
        static constexpr int CASCADE_COUNT = 4;
        
        // Плотная сетка для хорошего покрытия, но без фанатизма
        static constexpr int PROBE_X = 32; 
        static constexpr int PROBE_Y = 16;
        static constexpr int PROBE_Z = 32;
        
        // 16x16 = 256 лучей на зонд. Этого более чем достаточно для диффузного GI!
        static constexpr int OCTA_SIZE = 16; 
        
        // Длина луча 0-го каскада. (След. каскады будут 3.0, 6.0, 12.0)
        static constexpr float BASE_RAY_LENGTH = 1.5f; 
    };
    struct RCPushConstants
    {
        float4 probeGridMin; // 16 байт
        float4 probeGridMax; // 16 байт
        int probeCountX, probeCountY, probeCountZ, cascadeIndex;  // 16 байт
        // x: rayLength, y: screenWidth, z: screenHeight, w: (резерв)
        float4 params;       // 16 байт
    }; // Итого: 64 байта — идеально!
    class RadianceCascadesSystem
    {
    public:
        RadianceCascadesSystem(BurnhopeDevice &device,
                               VkExtent2D screenExtent,
                               BurnhopeDescriptorPool &pool,
                               VkDescriptorSetLayout globalLayout,
                               VkDescriptorSetLayout gBufferLayout,
                               VkImageView lightingImageView,
                               VkSampler lightingSampler,VkDescriptorSetLayout rtLayout,
        VkDescriptorSetLayout storageLayout, // ДОБАВЛЕНО
                               VkDescriptorSetLayout textureLayout,
                               int probeX, int probeY, int probeZ, int octaSize, float baseRayLength);
        ~RadianceCascadesSystem();
        
        bool needsRebuild(int pX, int pY, int pZ, int oSize, float bLen) const {
            return probeX != pX || probeY != pY || probeZ != pZ || octaSize != oSize || baseRayLength != bLen;
        }
        void updateConfig(int pX, int pY, int pZ, int oSize, float bLen) {
            probeX = pX; probeY = pY; probeZ = pZ; octaSize = oSize; baseRayLength = bLen;
        }

        RadianceCascadesSystem(const RadianceCascadesSystem &) = delete;
        RadianceCascadesSystem &operator=(const RadianceCascadesSystem &) = delete;
        void dispatch(VkCommandBuffer cmd,
                      VkDescriptorSet globalSet,
                      VkDescriptorSet gBufferSet,
                      const float4x4& invViewProj,
                      const float3& cameraPos,
                      const float3& sceneMin,
                      const float3& sceneMax,
                      VkExtent2D extent,
                      VkDescriptorSet rtSet,
                      VkDescriptorSet storageSet,
                      VkDescriptorSet textureSet);
        VkDescriptorSet getGISet() const { return giSet; }
        void rebuildOnResize(VkExtent2D newExtent,
                             VkImageView lightingImageView,
                             VkSampler lightingSampler);

    private:
        void createProbeTextures();
        void createGITextures();
        void createLayouts();
        void createPipelines(VkDescriptorSetLayout rtLayout,VkDescriptorSetLayout storageLayout, VkDescriptorSetLayout textureLayout);
        
        void createDescriptorSets(VkImageView lightingImageView, VkSampler lightingSampler);
        void insertBarrier(VkCommandBuffer cmd, VkImage image,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkAccessFlags src, VkAccessFlags dst,
                           VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
        BurnhopeDevice &device;
        BurnhopeDescriptorPool &pool;
        VkExtent2D screenExtent;
        
        int cascadeProbeX(int c) const { return std::max(1, probeX >> c); }
        int cascadeProbeY(int c) const { return std::max(1, probeY >> c); }
        int cascadeProbeZ(int c) const { return std::max(1, probeZ >> c); }
        int probeX, probeY, probeZ, octaSize;
        float baseRayLength;
        std::array<std::unique_ptr<BurnhopeTexture>, RCConfig::CASCADE_COUNT> probeTex;
        std::unique_ptr<BurnhopeTexture> diffuseGITex;
        std::unique_ptr<BurnhopeTexture> specularGITex;
        std::unique_ptr<BurnhopeDescriptorSetLayout> probeWriteLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> mergeLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> sampleWriteLayout;
        std::unique_ptr<BurnhopeDescriptorSetLayout> lightingReadLayout;
        VkPipelineLayout probeUpdatePL = VK_NULL_HANDLE;
        VkPipelineLayout mergePL = VK_NULL_HANDLE;
        VkPipelineLayout samplePL = VK_NULL_HANDLE;
        std::unique_ptr<BurnhopePipeline> probeUpdatePipeline;
        std::unique_ptr<BurnhopePipeline> mergePipeline;
        std::unique_ptr<BurnhopePipeline> samplePipeline;
        std::array<VkDescriptorSet, RCConfig::CASCADE_COUNT> probeWriteSets{};
        std::array<VkDescriptorSet, RCConfig::CASCADE_COUNT> mergeReadSets{};
        VkDescriptorSet giWriteSet = VK_NULL_HANDLE;
        VkDescriptorSet giSet = VK_NULL_HANDLE;
        VkDescriptorSet lightingReadSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout globalLayoutRef = VK_NULL_HANDLE;
        VkDescriptorSetLayout gBufferLayoutRef = VK_NULL_HANDLE;
        std::unique_ptr<ComputeShader> probeUpdateShader;
        std::unique_ptr<ComputeShader> mergeShader;
        std::unique_ptr<ComputeShader> sampleShader;
    };
}