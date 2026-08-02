#include "shadow.hpp"
#include <stdexcept>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
namespace burnhope
{
    namespace
    {

        void transitionImageToDepth(BurnhopeDevice &device, VkImage image, uint32_t layerCount, VkFormat format)
{
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    
    // По умолчанию берем глубину
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    
    // Если формат содержит трафарет (Stencil), обязательно забираем и его!
    if (format == VK_FORMAT_D32_SFLOAT_S8_UINT || 
        format == VK_FORMAT_D24_UNORM_S8_UINT || 
        format == VK_FORMAT_D16_UNORM_S8_UINT) 
    {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }

    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;
    
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    barrier.srcAccessMask = 0;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    
    VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &depInfo);
    device.endSingleTimeCommands(cmd);
}
    }
    BurnhopeShadowAtlas::BurnhopeShadowAtlas(BurnhopeDevice &dev) : device(dev)
    {
        createResources();
        
    }
    BurnhopeShadowAtlas::~BurnhopeShadowAtlas()
    {
    }
    void BurnhopeShadowAtlas::createResources()
    {
        VkExtent3D ext{ATLAS_RESOLUTION, ATLAS_RESOLUTION, 1};
        VkFormat depthFmt = device.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        atlasTexture = std::make_unique<BurnhopeTexture>(
            device, depthFmt, ext,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);
        transitionImageToDepth(device, atlasTexture->getImage(), 1, depthFmt);
    }

    void BurnhopeShadowAtlas::setTileViewport(VkCommandBuffer cmd, int pixelX, int pixelY, int tileSize) const
    {
        VkViewport vp{(float)pixelX, (float)pixelY, (float)tileSize, (float)tileSize, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D scissor{{pixelX, pixelY}, {(uint32_t)tileSize, (uint32_t)tileSize}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    VirtualShadowMap::VirtualShadowMap(BurnhopeDevice &dev) : device(dev)
    {
        createResources();
    }

    VirtualShadowMap::~VirtualShadowMap()
    {
    }

    void VirtualShadowMap::createResources()
    {
        uint32_t physRes = 32 * PAGE_SIZE;
        VkExtent3D ext{physRes, physRes, 1};
        VkFormat depthFmt = device.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        physicalAtlas = std::make_unique<BurnhopeTexture>(
            device, depthFmt, ext,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);
        transitionImageToDepth(device, physicalAtlas->getImage(), 1, depthFmt);

        pageTableBuffer = std::make_unique<BurnhopeBuffer>(
            device, sizeof(uint32_t), VIRTUAL_PAGES_X * VIRTUAL_PAGES_Y,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        physicalPageAllocator = std::make_unique<BurnhopeBuffer>(
            device, sizeof(uint32_t), MAX_PHYSICAL_PAGES + 1, // Индекс 0 выступает как atomic-счетчик
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    BurnhopeCSM::BurnhopeCSM(BurnhopeDevice &dev) : device(dev)
    {
        createResources();
       
    }
    BurnhopeCSM::~BurnhopeCSM()
    {
        for (int i = 0; i < CASCADE_COUNT; i++)
        {
            vkDestroyImageView(device.device(), cascadeViews[i], nullptr);
        }
      
    }
    void BurnhopeCSM::createResources()
    {
        VkExtent3D ext{SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
        VkFormat depthFmt = device.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        csmTexture = std::make_unique<BurnhopeTexture>(
            device, depthFmt, ext,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT, CASCADE_COUNT);
        transitionImageToDepth(device, csmTexture->getImage(), CASCADE_COUNT, depthFmt);
        for (int i = 0; i < CASCADE_COUNT; i++)
        {
            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = csmTexture->getImage();
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = csmTexture->getFormat();
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = i;
            viewInfo.subresourceRange.layerCount = 1;
            vkCreateImageView(device.device(), &viewInfo, nullptr, &cascadeViews[i]);
        }
    }

    std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT> BurnhopeCSM::calculateMatrices(
        const Camera &camera, glm::vec3 sunDir, const std::array<float, CASCADE_COUNT> &splits) const
    {
        std::array<glm::mat4, CASCADE_COUNT> result{};
        float nearP = 0.1f;
        for (int i = 0; i < CASCADE_COUNT; i++)
        {
            result[i] = calculateCascadeMatrix(nearP, splits[i], camera, sunDir, SHADOW_MAP_SIZE);
            nearP = splits[i];
        }
        return result;
    }
    glm::mat4 BurnhopeCSM::calculateCascadeMatrix(float nearP, float farP, const Camera &camera, glm::vec3 sunDir, float shadowSize) const
    {
        glm::mat4 proj = camera.GetProjectionMatrix(45.0f, nearP, farP);
        glm::mat4 invCam = glm::inverse(proj * camera.GetViewMatrix());
        std::vector<glm::vec4> corners;
        for (int x = 0; x < 2; x++)
            for (int y = 0; y < 2; y++)
                for (int z = 0; z < 2; z++)
                {
                    glm::vec4 pt = invCam * glm::vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, (float)z, 1.0f);
                    corners.push_back(pt / pt.w);
                }
        glm::vec3 center(0);
        for (auto &v : corners)
            center += glm::vec3(v);
        center /= 8.0f;
        float radius = 0.0f;
        for (auto &v : corners)
            radius = std::max(radius, glm::length(glm::vec3(v) - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;
        radius += glm::clamp(radius * 0.05f, 2.0f, 15.0f);
        glm::vec3 up = (std::abs(sunDir.y) > 0.999f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        float lightDistance = 2000.0f;
        glm::mat4 lightView = glm::lookAt(center - sunDir * lightDistance, center, up);
        float wupt = (radius * 2.0f) / shadowSize;
        glm::vec3 cls = glm::vec3(lightView * glm::vec4(center, 1.0f));
        cls.x = std::floor(cls.x / wupt) * wupt;
        cls.y = std::floor(cls.y / wupt) * wupt;
        center = glm::vec3(glm::inverse(lightView) * glm::vec4(cls, 1.0f));
        lightView = glm::lookAt(center - sunDir * lightDistance, center, up);
        glm::mat4 projs = glm::ortho(-radius, radius, -radius, radius, -2000.0f, lightDistance + radius + 500.0f);
        projs[1][1] *= -1;
        return projs * lightView;
    }
    BurnhopeShadowSystem::BurnhopeShadowSystem(BurnhopeDevice &dev) : device(dev)
    {
        shadowAtlas = std::make_unique<BurnhopeShadowAtlas>(dev);
        csm = std::make_unique<BurnhopeCSM>(dev);
        vsm = std::make_unique<VirtualShadowMap>(dev);
    }
    void BurnhopeShadowSystem::updateLights(entt::registry &registry, const glm::vec3 &camPos)
    {
        lightUBO = {};
        sunDir = glm::vec3(0, -1, 0);
        int allocX = 0, allocY = 0;
        const int atlasInUnits = BurnhopeShadowAtlas::ATLAS_IN_UNITS;
        const int minTile = BurnhopeShadowAtlas::MIN_TILE;
        auto lightView = registry.view<LightComponent, TransformComponent>();
        for (auto entity : lightView)
        {
            if (lightUBO.activeLightsCount >= 100)
                break;
            auto &lc = lightView.get<LightComponent>(entity).light;
            auto &tc = lightView.get<TransformComponent>(entity).transform;
            if (!lc.enable || lc.type == LightType::None)
                continue;
            // Must match Transform::rotationMatrix() (XYZ Euler), not glm::quat(euler) (YXZ).
            glm::vec3 dir = tc.rotateVector(glm::vec3(0.0f, -1.0f, 0.0f));
            if (lc.type == LightType::Directional)
            {
                sunDir = dir;
            }
            if (lc.castShadows && lc.type != LightType::Directional)
            {
                float dist = glm::length(tc.position - camPos);
                lc.shadowTileSize = (dist < 20.0f) ? 512 : (dist < 60.0f) ? 256
                                                                          : 128;
                int unitsPerTile = lc.shadowTileSize / minTile;
                int facesCount = (lc.type == LightType::Point) ? 6 : 1;
                if (allocX + unitsPerTile * facesCount > atlasInUnits)
                {
                    allocX = 0;
                    allocY += unitsPerTile;
                }
                if (allocY + unitsPerTile <= atlasInUnits)
                {
                    lc.shadowSlot = allocY * atlasInUnits + allocX;
                    allocX += unitsPerTile * facesCount;
                }
                else
                {
                    lc.shadowSlot = -1;
                }
            }
            if (lc.type == LightType::Spot)
            {
                glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
                glm::vec3 safePos = tc.position;
                if (std::abs(safePos.x) < 0.001f && std::abs(safePos.z) < 0.001f)
                    safePos.x += 0.001f;
                glm::mat4 lp = glm::perspective(glm::radians(lc.outerCone * 2.0f), 1.0f, 0.1f, lc.radius);
                lp[1][1] *= -1;
                lc.lightSpaceMatrix = lp * glm::lookAt(safePos, safePos + dir, up);
            }
            else if (lc.type == LightType::Point && lc.castShadows)
            {
                glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, lc.radius);
                const glm::vec3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                const glm::vec3 ups[6] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
                for (int f = 0; f < 6; f++)
                {
                    faceMatricesData[lightUBO.activeLightsCount].faces[f] = proj * glm::lookAt(tc.position, tc.position + dirs[f], ups[f]);
                }
            }
            LightGPUData &g = lightUBO.lights[lightUBO.activeLightsCount];
            g.posType = glm::vec4(tc.position, (float)lc.type);
            g.colorInt = glm::vec4(lc.color, lc.intensity);
            g.dirRadius = glm::vec4(dir, lc.radius);
            g.lightSpaceMatrix = lc.lightSpaceMatrix;
            float encodedSlot = (float)(lc.shadowSlot * 10000 + lc.shadowTileSize);
            g.shadowParams = glm::vec4(
                glm::cos(glm::radians(lc.innerCone)),
                glm::cos(glm::radians(lc.outerCone)),
                lc.castShadows ? 1.0f : 0.0f,
                encodedSlot);
            lightUBO.activeLightsCount++;
        }
    }
}