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

namespace burnhope {

// ============================================================
// Структуры, которые уходят в шейдер (должны совпадать с GLSL)
// ============================================================
struct LightGPUData {
    glm::vec4 posType;           // xyz = position, w = type (0=sun,1=point,2=spot)
    glm::vec4 colorInt;          // xyz = color, w = intensity
    glm::vec4 dirRadius;         // xyz = direction, w = radius
    glm::vec4 shadowParams;      // x=innerCone, y=outerCone, z=castShadows, w=encodedSlot
    glm::mat4 lightSpaceMatrix;
};
struct PointFaceMatrices {
    glm::mat4 faces[6];
};
struct LightUBOData {
    int activeLightsCount;
    int _pad[3];
    LightGPUData lights[100];
};

// ============================================================
// SHADOW ATLAS — для Point и Spot источников
// ============================================================
class BurnhopeShadowAtlas {
public:
    static constexpr int ATLAS_RESOLUTION = 4096;
    static constexpr int MIN_TILE         = 128;
    static constexpr int ATLAS_IN_UNITS   = ATLAS_RESOLUTION / MIN_TILE; // 32

    BurnhopeShadowAtlas(BurnhopeDevice& device);
    ~BurnhopeShadowAtlas();

    BurnhopeShadowAtlas(const BurnhopeShadowAtlas&) = delete;
    BurnhopeShadowAtlas& operator=(const BurnhopeShadowAtlas&) = delete;

    VkRenderPass getRenderPass()  const { return renderPass; }
    VkFramebuffer getFramebuffer() const { return framebuffer; }
    BurnhopeTexture* getTexture() { return atlasTexture.get(); }

    // Вызывается перед render pass — устанавливает viewport/scissor для тайла
    void setTileViewport(VkCommandBuffer cmd, int pixelX, int pixelY, int tileSize) const;
    // shadow.hpp:

private:
    void createResources();
    void createRenderPass();
    void createFramebuffer();

    BurnhopeDevice& device;
    std::unique_ptr<BurnhopeTexture> atlasTexture;
    VkRenderPass  renderPass  = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

// ============================================================
// CSM — Cascaded Shadow Maps для солнца (4 каскада)
// ============================================================
class BurnhopeCSM {
public:
    static constexpr int CASCADE_COUNT    = 4;
    static constexpr int SHADOW_MAP_SIZE  = 2048;

    BurnhopeCSM(BurnhopeDevice& device);
    ~BurnhopeCSM();

    BurnhopeCSM(const BurnhopeCSM&) = delete;
    BurnhopeCSM& operator=(const BurnhopeCSM&) = delete;

    VkRenderPass  getRenderPass()              const { return renderPass; }
    VkFramebuffer getFramebuffer(int cascade)  const { return framebuffers[cascade]; }
    BurnhopeTexture* getTexture()                    { return csmTexture.get(); }

    // Возвращает матрицы для всех 4 каскадов
    std::array<glm::mat4, CASCADE_COUNT> calculateMatrices(
        const Camera& camera,
        glm::vec3 sunDir,
        const std::array<float, CASCADE_COUNT>& splits) const;

private:
    glm::mat4 calculateCascadeMatrix(float nearP, float farP,
        const Camera& camera, glm::vec3 sunDir, float shadowSize) const;

    void createResources();
    void createRenderPass();
    void createFramebuffers();

    BurnhopeDevice& device;
    std::unique_ptr<BurnhopeTexture> csmTexture;

    // По одному framebuffer на каждый каскад (через VkImageView с baseArrayLayer)
    std::array<VkImageView,   CASCADE_COUNT> cascadeViews{};
    std::array<VkFramebuffer, CASCADE_COUNT> framebuffers{};

    VkRenderPass renderPass = VK_NULL_HANDLE;
};

// ============================================================
// SHADOW SYSTEM — оркестрирует всё вместе
// ============================================================
class BurnhopeShadowSystem {
public:
    BurnhopeShadowSystem(BurnhopeDevice& device);
    ~BurnhopeShadowSystem() = default;

    BurnhopeShadowSystem(const BurnhopeShadowSystem&) = delete;
    BurnhopeShadowSystem& operator=(const BurnhopeShadowSystem&) = delete;

    // Собирает LightUBOData из registry, раздаёт слоты в атласе
    void updateLights(entt::registry& registry, const glm::vec3& camPos);

    // Возвращает заполненный UBO для записи в буфер
    const LightUBOData& getLightUBO() const { return lightUBO; }

    // Матрицы каскадов (обновляются каждый кадр)
    const std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT>& getCascadeMatrices() const {
        return cascadeMatrices;
    }
    PointFaceMatrices faceMatricesData[100]{};
    const PointFaceMatrices* getFaceMatricesData() const { return faceMatricesData; }
    glm::vec3 getSunDir() const { return sunDir; }

    BurnhopeShadowAtlas* getAtlas() { return shadowAtlas.get(); }
    BurnhopeCSM*         getCSM()   { return csm.get(); }

    std::array<float, 4> cascadeSplits = { 25.0f, 80.0f, 200.0f, 400.0f };

private:
    BurnhopeDevice& device;
    std::unique_ptr<BurnhopeShadowAtlas> shadowAtlas;
    std::unique_ptr<BurnhopeCSM>         csm;

    LightUBOData lightUBO{};
    std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT> cascadeMatrices{};
    glm::vec3 sunDir{ 0.0f, -1.0f, 0.0f };
};

} // namespace burnhope
