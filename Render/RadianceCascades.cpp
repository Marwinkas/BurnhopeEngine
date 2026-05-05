#include "RadianceCascades.hpp"
#include "ComputeShader.hpp"
#include <stdexcept>
#include <array>
namespace burnhope
{
    static int cascadeProbeX(int cascade) { return std::max(1, RCConfig::PROBE_X >> cascade); }
    static int cascadeProbeY(int cascade) { return std::max(1, RCConfig::PROBE_Y >> cascade); }
    static int cascadeProbeZ(int cascade) { return std::max(1, RCConfig::PROBE_Z >> cascade); }
    RadianceCascadesSystem::RadianceCascadesSystem(
        BurnhopeDevice &device,
        VkExtent2D screenExtent,
        BurnhopeDescriptorPool &pool,
        VkDescriptorSetLayout globalLayout,
        VkDescriptorSetLayout gBufferLayout,
        VkImageView lightingImageView,
        VkSampler lightingSampler,VkDescriptorSetLayout rtLayout,
        VkDescriptorSetLayout storageLayout, // ДОБАВЛЕНО
        VkDescriptorSetLayout textureLayout)
        : device(device), pool(pool), screenExtent(screenExtent),
          globalLayoutRef(globalLayout), gBufferLayoutRef(gBufferLayout)
    {
        createProbeTextures();
        createIrradianceTexture();
        createLayouts();
        createPipelines(rtLayout, storageLayout, textureLayout);
        createDescriptorSets(lightingImageView, lightingSampler);
    }
    RadianceCascadesSystem::~RadianceCascadesSystem() = default;
    void RadianceCascadesSystem::createProbeTextures()
    {
        for (int c = 0; c < RCConfig::CASCADE_COUNT; c++)
        {
            int px = cascadeProbeX(c);
            int py = cascadeProbeY(c);
            int pz = cascadeProbeZ(c);
            uint32_t w = px * RCConfig::OCTA_SIZE;
            uint32_t h = py * pz * RCConfig::OCTA_SIZE;
            probeTex[c] = std::make_unique<BurnhopeTexture>(
                device,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                VkExtent3D{w, h, 1},
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_SAMPLE_COUNT_1_BIT);
            VkCommandBuffer cmd = device.beginSingleTimeCommands();
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = probeTex[c]->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
            device.endSingleTimeCommands(cmd);
        }
    }
    void RadianceCascadesSystem::createIrradianceTexture()
    {
        irradianceTex = std::make_unique<BurnhopeTexture>(
            device,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{screenExtent.width, screenExtent.height, 1},
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);
        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = irradianceTex->getImage();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        device.endSingleTimeCommands(cmd);
    }
    void RadianceCascadesSystem::createLayouts()
    {
        probeWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .build();
        mergeLayout = BurnhopeDescriptorSetLayout::Builder(device)
                          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                          .build();
        sampleWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
                                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                .build();
    }
    void RadianceCascadesSystem::createPipelines(VkDescriptorSetLayout rtLayout,VkDescriptorSetLayout storageLayout, VkDescriptorSetLayout textureLayout)
    {
        // В createPipelines:
        std::vector<VkDescriptorSetLayout> updateLayouts = {
            globalLayoutRef, 
            gBufferLayoutRef, 
            probeWriteLayout->getDescriptorSetLayout(),
            rtLayout,
            storageLayout, // НОВОЕ: (Set = 4) renderSystem.getRenderSystemLayout()
            textureLayout  // НОВОЕ: (Set = 5) renderSystem.getTextureLayout()
        };
        probeUpdateShader = std::make_unique<ComputeShader>(
            device, "shaders/probe_update.comp.spv", updateLayouts, sizeof(RCPushConstants));
        std::vector<VkDescriptorSetLayout> mergeLayouts = {mergeLayout->getDescriptorSetLayout()};
        mergeShader = std::make_unique<ComputeShader>(
            device, "shaders/cascade_merge.comp.spv", mergeLayouts, sizeof(RCPushConstants));
        std::vector<VkDescriptorSetLayout> sampleLayouts = {globalLayoutRef, gBufferLayoutRef, sampleWriteLayout->getDescriptorSetLayout()};
        sampleShader = std::make_unique<ComputeShader>(
            device, "shaders/irradiance_sample.comp.spv", sampleLayouts, sizeof(RCPushConstants));
    }
    void RadianceCascadesSystem::createDescriptorSets(VkImageView lightingImageView, VkSampler lightingSampler)
    {
        VkDescriptorImageInfo lightInfo{};
        lightInfo.imageView = lightingImageView;
        lightInfo.sampler = lightingSampler;
        // This image is sampled in probe_update.comp, so keep descriptor layout explicit.
        lightInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        for (int c = 0; c < RCConfig::CASCADE_COUNT; c++)
        {
            VkDescriptorImageInfo probeWriteInfo{};
            probeWriteInfo.imageView = probeTex[c]->getImageView();
            probeWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            BurnhopeDescriptorWriter(*probeWriteLayout, pool)
                .writeImage(0, &probeWriteInfo)
                .writeImage(1, &lightInfo)
                .build(probeWriteSets[c]);
        }
        for (int c = 0; c < RCConfig::CASCADE_COUNT; c++)
        {
            int nextC = std::min(c + 1, RCConfig::CASCADE_COUNT - 1);
            VkDescriptorImageInfo writeInfo{};
            writeInfo.imageView = probeTex[c]->getImageView();
            writeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo readInfo{};
            readInfo.imageView = probeTex[nextC]->getImageView();
            readInfo.sampler = probeTex[nextC]->getSampler();
            readInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            BurnhopeDescriptorWriter(*mergeLayout, pool)
                .writeImage(0, &writeInfo)
                .writeImage(1, &readInfo)
                .build(mergeReadSets[c]);
        }
        {
            VkDescriptorImageInfo irWriteInfo{};
            irWriteInfo.imageView = irradianceTex->getImageView();
            irWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            VkDescriptorImageInfo cascade0ReadInfo{};
            cascade0ReadInfo.imageView = probeTex[0]->getImageView();
            cascade0ReadInfo.sampler = probeTex[0]->getSampler();
            cascade0ReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            BurnhopeDescriptorWriter(*sampleWriteLayout, pool)
                .writeImage(0, &irWriteInfo)
                .writeImage(1, &cascade0ReadInfo)
                .build(irradianceWriteSet);
        }
        auto irReadLayout = BurnhopeDescriptorSetLayout::Builder(device)
                                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                .build();
        VkDescriptorImageInfo irReadInfo{};
        irReadInfo.imageView = irradianceTex->getImageView();
        irReadInfo.sampler = irradianceTex->getSampler();
        irReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*irReadLayout, pool)
            .writeImage(0, &irReadInfo)
            .build(irradianceSet);
    }
    void RadianceCascadesSystem::insertBarrier(
        VkCommandBuffer cmd, VkImage image,
        VkImageLayout oldLayout, VkImageLayout newLayout,
        VkAccessFlags src, VkAccessFlags dst,
        VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.oldLayout = oldLayout;
        b.newLayout = newLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    void RadianceCascadesSystem::dispatch(
        VkCommandBuffer cmd,
        VkDescriptorSet globalSet,
        VkDescriptorSet gBufferSet,
        const glm::mat4 &invViewProj,
        const glm::vec3 &cameraPos,
        const glm::vec3 &sceneMin,
        const glm::vec3 &sceneMax,
        VkExtent2D extent,
    VkDescriptorSet rtSet,
        VkDescriptorSet storageSet, // ДОБАВЛЕНО
        VkDescriptorSet textureSet)
    {
        probeUpdateShader->bind(cmd);
        for (int c = RCConfig::CASCADE_COUNT - 1; c >= 0; c--)
        {
            RCPushConstants push{};
            push.probeGridMin = glm::vec4(sceneMin, 0.0f);
            push.probeGridMax = glm::vec4(sceneMax, 0.0f);
            push.probeCount = glm::ivec4(cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c), c);
            push.params = glm::vec4(
                RCConfig::BASE_RAY_LENGTH * std::pow(2.0f, (float)c), // Это твой rayLength
                (float)extent.width, 
                (float)extent.height, 
                0.0f
            );
            probeUpdateShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
            probeUpdateShader->bindDescriptorSets(cmd, {globalSet, gBufferSet, probeWriteSets[c], rtSet, storageSet, textureSet});
            int px = cascadeProbeX(c);
            int pz = cascadeProbeZ(c);
            uint32_t w = px * RCConfig::OCTA_SIZE;
            uint32_t h = cascadeProbeY(c) * pz * RCConfig::OCTA_SIZE;
            probeUpdateShader->dispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
            insertBarrier(cmd, probeTex[c]->getImage(),
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        mergeShader->bind(cmd);
        for (int c = RCConfig::CASCADE_COUNT - 2; c >= 0; c--)
        {
            RCPushConstants push{};
            push.probeCount = glm::ivec4(cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c), c);
            push.params.w = (float)c;
            mergeShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
            mergeShader->bindDescriptorSets(cmd, {mergeReadSets[c]});
            uint32_t w = cascadeProbeX(c) * RCConfig::OCTA_SIZE;
            uint32_t h = cascadeProbeY(c) * cascadeProbeZ(c) * RCConfig::OCTA_SIZE;
            mergeShader->dispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
            insertBarrier(cmd, probeTex[c]->getImage(),
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }
        sampleShader->bind(cmd);
        RCPushConstants push{};
        push.probeGridMin = glm::vec4(sceneMin, 0.0f);
        push.probeGridMax = glm::vec4(sceneMax, 0.0f);
        push.probeCount = glm::ivec4(cascadeProbeX(0), cascadeProbeY(0), cascadeProbeZ(0), 0);
        push.params = glm::vec4(
            RCConfig::BASE_RAY_LENGTH * std::pow(2.0f, 1), // Это твой rayLength
            (float)extent.width, 
            (float)extent.height, 
            0.0f
        );
        sampleShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
        sampleShader->bindDescriptorSets(cmd, {globalSet, gBufferSet, irradianceWriteSet});
        sampleShader->dispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);

        insertBarrier(cmd, irradianceTex->getImage(),
    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    
    }
    void RadianceCascadesSystem::rebuildOnResize(VkExtent2D newExtent,
                                                 VkImageView lightingImageView,
                                                 VkSampler lightingSampler)
    {
        screenExtent = newExtent;
        vkDeviceWaitIdle(device.device());
        createIrradianceTexture();
        createDescriptorSets(lightingImageView, lightingSampler);
    }
}