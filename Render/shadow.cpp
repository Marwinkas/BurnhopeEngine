#include "shadow.hpp"
#include <stdexcept>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>

namespace burnhope {




BurnhopeShadowAtlas::BurnhopeShadowAtlas(BurnhopeDevice& dev) : device(dev) {
    createResources();
    createRenderPass();
    createFramebuffer();
}

BurnhopeShadowAtlas::~BurnhopeShadowAtlas() {
    vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
    vkDestroyRenderPass(device.device(), renderPass, nullptr);
}

void BurnhopeShadowAtlas::createResources() {
    VkExtent3D ext{ ATLAS_RESOLUTION, ATLAS_RESOLUTION, 1 };
    VkFormat depthFmt = device.findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    atlasTexture = std::make_unique<BurnhopeTexture>(
        device, depthFmt, ext,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT);

    
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = atlasTexture->getImage();
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    device.endSingleTimeCommands(cmd);
}

void BurnhopeShadowAtlas::createRenderPass() {
    VkAttachmentDescription depth{};
    depth.format         = atlasTexture->getFormat();
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass      = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    deps[1].srcSubpass      = 0;
    deps[1].dstSubpass      = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &depth;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = (uint32_t)deps.size();
    rpInfo.pDependencies   = deps.data();

    if (vkCreateRenderPass(device.device(), &rpInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow atlas render pass!");
}

void BurnhopeShadowAtlas::createFramebuffer() {
    VkImageView view = atlasTexture->getImageView();

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass      = renderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments    = &view;
    fbInfo.width           = ATLAS_RESOLUTION;
    fbInfo.height          = ATLAS_RESOLUTION;
    fbInfo.layers          = 1;

    if (vkCreateFramebuffer(device.device(), &fbInfo, nullptr, &framebuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow atlas framebuffer!");
}

void BurnhopeShadowAtlas::setTileViewport(VkCommandBuffer cmd,
    int pixelX, int pixelY, int tileSize) const
{
    VkViewport vp{};
    vp.x        = (float)pixelX;
    vp.y        = (float)pixelY;
    vp.width    = (float)tileSize;
    vp.height   = (float)tileSize;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = { pixelX, pixelY };
    scissor.extent = { (uint32_t)tileSize, (uint32_t)tileSize };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}




BurnhopeCSM::BurnhopeCSM(BurnhopeDevice& dev) : device(dev) {
    createResources();
    createRenderPass();
    createFramebuffers();
}

BurnhopeCSM::~BurnhopeCSM() {
    for (int i = 0; i < CASCADE_COUNT; i++) {
        vkDestroyFramebuffer(device.device(), framebuffers[i], nullptr);
        vkDestroyImageView(device.device(), cascadeViews[i], nullptr);
    }
    vkDestroyRenderPass(device.device(), renderPass, nullptr);
}

void BurnhopeCSM::createResources() {
    VkExtent3D ext{ SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1 };
    VkFormat depthFmt = device.findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    
    csmTexture = std::make_unique<BurnhopeTexture>(
        device, depthFmt, ext,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        CASCADE_COUNT); 

    
    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = csmTexture->getImage();
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, CASCADE_COUNT };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    device.endSingleTimeCommands(cmd);
}

void BurnhopeCSM::createRenderPass() {
    VkAttachmentDescription depth{};
    depth.format         = csmTexture->getFormat();
    depth.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;   
    depth.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 0;
    subpass.pDepthStencilAttachment = &depthRef;

    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass    = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    deps[1].srcSubpass    = 0;
    deps[1].dstSubpass    = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments    = &depth;
    rpInfo.subpassCount    = 1;
    rpInfo.pSubpasses      = &subpass;
    rpInfo.dependencyCount = (uint32_t)deps.size();
    rpInfo.pDependencies   = deps.data();

    if (vkCreateRenderPass(device.device(), &rpInfo, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create CSM render pass!");
}

void BurnhopeCSM::createFramebuffers() {
    for (int i = 0; i < CASCADE_COUNT; i++) {
        
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = csmTexture->getImage();
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = csmTexture->getFormat();
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = i;   
        viewInfo.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &cascadeViews[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create CSM cascade image view!");

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = &cascadeViews[i];
        fbInfo.width           = SHADOW_MAP_SIZE;
        fbInfo.height          = SHADOW_MAP_SIZE;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(device.device(), &fbInfo, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create CSM cascade framebuffer!");
    }
}

std::array<glm::mat4, BurnhopeCSM::CASCADE_COUNT> BurnhopeCSM::calculateMatrices(
    const Camera& camera, glm::vec3 sunDir,
    const std::array<float, CASCADE_COUNT>& splits) const
{
    std::array<glm::mat4, CASCADE_COUNT> result{};
    float nearP = 0.1f;
    for (int i = 0; i < CASCADE_COUNT; i++) {
        result[i] = calculateCascadeMatrix(nearP, splits[i], camera, sunDir, SHADOW_MAP_SIZE);
        nearP = splits[i];
    }
    return result;
}

glm::mat4 BurnhopeCSM::calculateCascadeMatrix(float nearP, float farP,
    const Camera& camera, glm::vec3 sunDir, float shadowSize) const
{
    
    glm::mat4 proj    = camera.GetProjectionMatrix(45.0f, nearP, farP);
    glm::mat4 invCam  = glm::inverse(proj * camera.GetViewMatrix());

    std::vector<glm::vec4> corners;
    for (int x = 0; x < 2; x++)
        for (int y = 0; y < 2; y++)
            for (int z = 0; z < 2; z++) {
                glm::vec4 pt = invCam * glm::vec4(
                    2.0f * x - 1.0f, 
                    2.0f * y - 1.0f, 
                    (float)z,        
                    1.0f);
                corners.push_back(pt / pt.w);
            }

    glm::vec3 center(0);
    for (auto& v : corners) center += glm::vec3(v);
    center /= 8.0f;

    float radius = 0.0f;
    for (auto& v : corners)
        radius = std::max(radius, glm::length(glm::vec3(v) - center));

    radius = std::ceil(radius * 16.0f) / 16.0f;

    float padding = glm::clamp(radius * 0.05f, 2.0f, 15.0f);
    radius += padding;

    glm::vec3 up = glm::vec3(0, 1, 0);
    if (std::abs(sunDir.y) > 0.999f) up = glm::vec3(0, 0, 1);

    
    float lightDistance = 2000.0f;
    glm::mat4 lightView = glm::lookAt(center - sunDir * lightDistance, center, up);

    
    float wupt = (radius * 2.0f) / shadowSize;
    glm::vec3 cls = glm::vec3(lightView * glm::vec4(center, 1.0f));
    cls.x = std::floor(cls.x / wupt) * wupt;
    cls.y = std::floor(cls.y / wupt) * wupt;
    center = glm::vec3(glm::inverse(lightView) * glm::vec4(cls, 1.0f));

    
    lightView = glm::lookAt(center - sunDir * lightDistance, center, up);

    
    
    
    float zNear = -2000.0f;
    float zFar = lightDistance + radius + 500.0f;

    glm::mat4 projs = glm::ortho(-radius, radius, -radius, radius, zNear, zFar);
    projs[1][1] *= -1; 
    return projs * lightView;
}




BurnhopeShadowSystem::BurnhopeShadowSystem(BurnhopeDevice& dev) : device(dev) {
    shadowAtlas = std::make_unique<BurnhopeShadowAtlas>(dev);
    csm         = std::make_unique<BurnhopeCSM>(dev);
}

void BurnhopeShadowSystem::updateLights(entt::registry& registry, const glm::vec3& camPos) {
    lightUBO = {};

    int allocX = 0, allocY = 0;
    const int atlasInUnits = BurnhopeShadowAtlas::ATLAS_IN_UNITS;
    const int minTile      = BurnhopeShadowAtlas::MIN_TILE;

    
    auto lightView = registry.view<LightComponent, TransformComponent>();
    for (auto entity : lightView) {
        auto& light     = lightView.get<LightComponent>(entity).light;
        auto& transform = lightView.get<TransformComponent>(entity).transform;

        if (!light.enable || !light.castShadows || light.type == LightType::Directional) continue;

        float dist = glm::length(transform.position - camPos);
        int tileSize = (dist < 20.0f) ? 512 : (dist < 60.0f) ? 256 : 128;
        light.shadowTileSize = tileSize;

        int unitsPerTile = tileSize / minTile;
        int facesCount   = (light.type == LightType::Point) ? 6 : 1;

        if (allocX + unitsPerTile * facesCount > atlasInUnits) {
            allocX  = 0;
            allocY += unitsPerTile;
        }
        if (allocY + unitsPerTile > atlasInUnits) {
            light.shadowSlot = -1;
            continue;
        }
        light.shadowSlot = allocY * atlasInUnits + allocX;
        allocX += unitsPerTile * facesCount;
    }

    
    sunDir = glm::vec3(0, -1, 0);
    for (auto entity : lightView) {
        auto& lc = lightView.get<LightComponent>(entity).light;
        auto& tc = lightView.get<TransformComponent>(entity).transform;
        if (!lc.enable || lc.type == LightType::None) continue;
        if (lightUBO.activeLightsCount >= 100) break;

        if (lc.type == LightType::Directional) {
            glm::quat q  = glm::quat(glm::radians(tc.rotation));
            sunDir        = glm::normalize(q * glm::vec3(0, -1, 0));
        }

        int i = lightUBO.activeLightsCount;
        LightGPUData& g = lightUBO.lights[i];
     
        g.posType   = glm::vec4(tc.position, (float)lc.type);
        g.colorInt  = glm::vec4(lc.color, lc.intensity);

        glm::quat q  = glm::quat(glm::radians(tc.rotation));
        glm::vec3 dir = glm::normalize(q * glm::vec3(0, -1, 0));
        g.dirRadius  = glm::vec4(dir, lc.radius);

        if (lc.type == LightType::Spot) {
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    if (std::abs(dir.y) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    
    glm::vec3 safePos = tc.position;
    if (std::abs(safePos.x) < 0.001f && std::abs(safePos.z) < 0.001f) {
        safePos.x += 0.001f;
    }

    glm::mat4 lp = glm::perspective(glm::radians(lc.outerCone * 2.0f), 1.0f, 0.1f, lc.radius);
    lp[1][1] *= -1;
    glm::mat4 lv = glm::lookAt(safePos, safePos + dir, up);
    lc.lightSpaceMatrix = lp * lv;
}
        float encodedSlot = (float)(lc.shadowSlot * 10000 + lc.shadowTileSize);
        g.shadowParams   = glm::vec4(
            glm::cos(glm::radians(lc.innerCone)),
            glm::cos(glm::radians(lc.outerCone)),
            lc.castShadows ? 1.0f : 0.0f,
            encodedSlot);
        g.lightSpaceMatrix = lc.lightSpaceMatrix;
            if (lc.type == LightType::Point && lc.castShadows) {
                glm::vec3 pos = tc.position;
                glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, lc.radius);
                
                const glm::vec3 dirs[6] = {
                    {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
                };
                const glm::vec3 ups[6] = {
                    {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
                };
                
                int lightIdx = lightUBO.activeLightsCount; 
                for (int f = 0; f < 6; f++) {
                    faceMatricesData[lightIdx].faces[f] = 
                        proj * glm::lookAt(pos, pos + dirs[f], ups[f]);
                }
            }
        lightUBO.activeLightsCount++;
    }
}

} 
