#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Descriptors.hpp"
#include "Texture.hpp"
#include "../Utils/Pipeline.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <vector>

namespace burnhope {

// Конфигурация — меняй под свою сцену
struct RCConfig {
    static constexpr int   CASCADE_COUNT    = 4;
    static constexpr int   PROBE_X          = 24;   // зондов по X для каскада 0
    static constexpr int   PROBE_Y          = 12;   // по Y
    static constexpr int   PROBE_Z          = 24;   // по Z
    static constexpr int   OCTA_SIZE        = 8;    // разрешение октаэдра одного зонда
    static constexpr float BASE_RAY_LENGTH  = 10.0f; // дальность луча каскада 0 в метрах
};

// UBO передаётся в каждый шейдер каскада
struct RCPushConstants {
    glm::mat4 invViewProj;
    glm::vec4 cameraPos;
    glm::vec4 probeGridMin;    // AABB сцены
    glm::vec4 probeGridMax;
    glm::ivec4 probeCount;     // x,y,z, cascadeIndex
    float rayLength;
    float cascadeIndex;
    float screenWidth;
    float screenHeight;
};

class RadianceCascadesSystem {
public:
    RadianceCascadesSystem(BurnhopeDevice& device,
                           VkExtent2D screenExtent,
                           BurnhopeDescriptorPool& pool,
                           VkDescriptorSetLayout globalLayout,
                           VkDescriptorSetLayout gBufferLayout,
                           VkImageView lightingImageView,   // hdrOutputTexture после lighting pass
                           VkSampler   lightingSampler);
    ~RadianceCascadesSystem();

    RadianceCascadesSystem(const RadianceCascadesSystem&) = delete;
    RadianceCascadesSystem& operator=(const RadianceCascadesSystem&) = delete;

    // Главный метод — вызывается каждый кадр
    void dispatch(VkCommandBuffer cmd,
                  VkDescriptorSet globalSet,
                  VkDescriptorSet gBufferSet,
                  const glm::mat4& invViewProj,
                  const glm::vec3& cameraPos,
                  const glm::vec3& sceneMin,
                  const glm::vec3& sceneMax,
                  VkExtent2D extent);

    // Финальная irradiance — добавляется в lighting.comp
    VkDescriptorSet getIrradianceSet() const { return irradianceSet; }

    void rebuildOnResize(VkExtent2D newExtent,
                         VkImageView lightingImageView,
                         VkSampler   lightingSampler);

private:
    void createProbeTextures();
    void createIrradianceTexture();
    void createLayouts();
    void createPipelines();
    void createDescriptorSets(VkImageView lightingImageView, VkSampler lightingSampler);
    void insertBarrier(VkCommandBuffer cmd, VkImage image,
                       VkImageLayout oldLayout, VkImageLayout newLayout,
                       VkAccessFlags src, VkAccessFlags dst,
                       VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

    BurnhopeDevice&        device;
    BurnhopeDescriptorPool& pool;
    VkExtent2D             screenExtent;

    // Текстуры зондов: размер = (probeX*octaSize) x (probeY*probeZ*octaSize)
    // Каждый каскад — отдельная текстура
    std::array<std::unique_ptr<BurnhopeTexture>, RCConfig::CASCADE_COUNT> probeTex;

    // Финальная irradiance (screen-space, RGBA16F)
    std::unique_ptr<BurnhopeTexture> irradianceTex;

    // Layouts
    std::unique_ptr<BurnhopeDescriptorSetLayout> probeWriteLayout;   // set 0 probe_update
    std::unique_ptr<BurnhopeDescriptorSetLayout> mergeLayout;        // set 0 cascade_merge
    std::unique_ptr<BurnhopeDescriptorSetLayout> sampleWriteLayout;  // set 0 irradiance_sample
    std::unique_ptr<BurnhopeDescriptorSetLayout> lightingReadLayout; // lighting texture

    // Pipeline layouts
    VkPipelineLayout probeUpdatePL   = VK_NULL_HANDLE;
    VkPipelineLayout mergePL         = VK_NULL_HANDLE;
    VkPipelineLayout samplePL        = VK_NULL_HANDLE;

    // Pipelines
    std::unique_ptr<BurnhopePipeline> probeUpdatePipeline;
    std::unique_ptr<BurnhopePipeline> mergePipeline;
    std::unique_ptr<BurnhopePipeline> samplePipeline;

    // Descriptor sets
    std::array<VkDescriptorSet, RCConfig::CASCADE_COUNT> probeWriteSets{};
    std::array<VkDescriptorSet, RCConfig::CASCADE_COUNT> mergeReadSets{};  // читаем cascade[i+1]
    VkDescriptorSet irradianceWriteSet = VK_NULL_HANDLE;
    VkDescriptorSet irradianceSet      = VK_NULL_HANDLE; // для lighting.comp (читаем)
    VkDescriptorSet lightingReadSet    = VK_NULL_HANDLE; // hdrOutputTexture

    // Сохраняем layouts для внешнего использования
    VkDescriptorSetLayout globalLayoutRef  = VK_NULL_HANDLE;
    VkDescriptorSetLayout gBufferLayoutRef = VK_NULL_HANDLE;
};

} // namespace burnhope