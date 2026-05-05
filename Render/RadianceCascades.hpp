#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Descriptors.hpp"
#include "Texture.hpp"
#include "ComputeShader.hpp"
#include "../Utils/Pipeline.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>
namespace burnhope
{
    struct RCConfig
    {
        static constexpr int CASCADE_COUNT = 4;
        // Увеличиваем количество зондов (было 24-12-24)
        static constexpr int PROBE_X = 48; 
        static constexpr int PROBE_Y = 24;
        static constexpr int PROBE_Z = 48;
        static constexpr int OCTA_SIZE = 24;
        static constexpr float BASE_RAY_LENGTH = 10.0f; // Уменьшим базу для четкости ближних теней
    };
    struct RCPushConstants
    {
        glm::vec4 probeGridMin; // 16 байт
        glm::vec4 probeGridMax; // 16 байт
        glm::ivec4 probeCount;  // 16 байт (x, y, z, cascadeIndex)
        // x: rayLength, y: screenWidth, z: screenHeight, w: (резерв)
        glm::vec4 params;       // 16 байт
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
        VkDescriptorSetLayout textureLayout);
        ~RadianceCascadesSystem();
        RadianceCascadesSystem(const RadianceCascadesSystem &) = delete;
        RadianceCascadesSystem &operator=(const RadianceCascadesSystem &) = delete;
        void dispatch(VkCommandBuffer cmd,
                      VkDescriptorSet globalSet,
                      VkDescriptorSet gBufferSet,
                      const glm::mat4 &invViewProj,
                      const glm::vec3 &cameraPos,
                      const glm::vec3 &sceneMin,
                      const glm::vec3 &sceneMax,
                      VkExtent2D extent,VkDescriptorSet rtSet,
        VkDescriptorSet storageSet, // ДОБАВЛЕНО
        VkDescriptorSet textureSet);
        VkDescriptorSet getIrradianceSet() const { return irradianceSet; }
        void rebuildOnResize(VkExtent2D newExtent,
                             VkImageView lightingImageView,
                             VkSampler lightingSampler);

    private:
        void createProbeTextures();
        void createIrradianceTexture();
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
        std::array<std::unique_ptr<BurnhopeTexture>, RCConfig::CASCADE_COUNT> probeTex;
        std::unique_ptr<BurnhopeTexture> irradianceTex;
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
        VkDescriptorSet irradianceWriteSet = VK_NULL_HANDLE;
        VkDescriptorSet irradianceSet = VK_NULL_HANDLE;
        VkDescriptorSet lightingReadSet = VK_NULL_HANDLE;
        VkDescriptorSetLayout globalLayoutRef = VK_NULL_HANDLE;
        VkDescriptorSetLayout gBufferLayoutRef = VK_NULL_HANDLE;
        std::unique_ptr<ComputeShader> probeUpdateShader;
        std::unique_ptr<ComputeShader> mergeShader;
        std::unique_ptr<ComputeShader> sampleShader;
    };
}