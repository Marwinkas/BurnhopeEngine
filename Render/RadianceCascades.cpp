#include "RadianceCascades.hpp"
#include <stdexcept>
#include <array>

namespace burnhope {

// Размер текстуры зондов каскада с учётом масштаба
static int cascadeProbeX(int cascade) { return std::max(1, RCConfig::PROBE_X >> cascade); }
static int cascadeProbeY(int cascade) { return std::max(1, RCConfig::PROBE_Y >> cascade); }
static int cascadeProbeZ(int cascade) { return std::max(1, RCConfig::PROBE_Z >> cascade); }

RadianceCascadesSystem::RadianceCascadesSystem(
    BurnhopeDevice& device,
    VkExtent2D screenExtent,
    BurnhopeDescriptorPool& pool,
    VkDescriptorSetLayout globalLayout,
    VkDescriptorSetLayout gBufferLayout,
    VkImageView lightingImageView,
    VkSampler   lightingSampler)
    : device(device), pool(pool), screenExtent(screenExtent),
      globalLayoutRef(globalLayout), gBufferLayoutRef(gBufferLayout)
{
    createProbeTextures();
    createIrradianceTexture();
    createLayouts();
    createPipelines();
    createDescriptorSets(lightingImageView, lightingSampler);
}

RadianceCascadesSystem::~RadianceCascadesSystem() {
    vkDestroyPipelineLayout(device.device(), probeUpdatePL, nullptr);
    vkDestroyPipelineLayout(device.device(), mergePL, nullptr);
    vkDestroyPipelineLayout(device.device(), samplePL, nullptr);
}

void RadianceCascadesSystem::createProbeTextures() {
    for (int c = 0; c < RCConfig::CASCADE_COUNT; c++) {
        int px = cascadeProbeX(c);
        int py = cascadeProbeY(c);
        int pz = cascadeProbeZ(c);

        uint32_t w = px * RCConfig::OCTA_SIZE;
        uint32_t h = py * pz * RCConfig::OCTA_SIZE;

        probeTex[c] = std::make_unique<BurnhopeTexture>(
            device,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{ w, h, 1 },
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);

        // Переводим в GENERAL для compute записи
        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image               = probeTex[c]->getImage();
        barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        barrier.srcAccessMask       = 0;
        barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
        device.endSingleTimeCommands(cmd);
    }
}

void RadianceCascadesSystem::createIrradianceTexture() {
    irradianceTex = std::make_unique<BurnhopeTexture>(
        device,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VkExtent3D{ screenExtent.width, screenExtent.height, 1 },
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT);

    VkCommandBuffer cmd = device.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout           = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = irradianceTex->getImage();
    barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask       = 0;
    barrier.dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
    device.endSingleTimeCommands(cmd);
}

void RadianceCascadesSystem::createLayouts() {
    // Layout для probe_update: пишем в текущий каскад, читаем lighting
    probeWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,         VK_SHADER_STAGE_COMPUTE_BIT) // write probe
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // read lighting
        .build();

    // Layout для cascade_merge: читаем следующий каскад, пишем в текущий
    mergeLayout = BurnhopeDescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT) // write current
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_COMPUTE_BIT) // read next
        .build();

    // Layout для irradiance_sample: пишем irradiance, читаем cascade 0
    sampleWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT) // write irradiance
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  VK_SHADER_STAGE_COMPUTE_BIT) // read cascade0
        .build();
}

void RadianceCascadesSystem::createPipelines() {
    // Push constants одни для всех — RCPushConstants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(RCPushConstants);

    // --- probe_update pipeline ---
    {
        std::vector<VkDescriptorSetLayout> layouts = {
            globalLayoutRef,                           // set 0: global UBO
            gBufferLayoutRef,                          // set 1: gBuffer
            probeWriteLayout->getDescriptorSetLayout() // set 2: probe write + lighting read
        };
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = (uint32_t)layouts.size();
        li.pSetLayouts = layouts.data();
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device.device(), &li, nullptr, &probeUpdatePL);

        probeUpdatePipeline = std::make_unique<BurnhopePipeline>(
            device, "shaders/probe_update.comp.spv", probeUpdatePL);
    }

    // --- cascade_merge pipeline ---
    {
        std::vector<VkDescriptorSetLayout> layouts = {
            mergeLayout->getDescriptorSetLayout() // set 0: write current + read next
        };
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = (uint32_t)layouts.size();
        li.pSetLayouts = layouts.data();
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device.device(), &li, nullptr, &mergePL);

        mergePipeline = std::make_unique<BurnhopePipeline>(
            device, "shaders/cascade_merge.comp.spv", mergePL);
    }

    // --- irradiance_sample pipeline ---
    {
        std::vector<VkDescriptorSetLayout> layouts = {
            globalLayoutRef,
            gBufferLayoutRef,
            sampleWriteLayout->getDescriptorSetLayout()
        };
        VkPipelineLayoutCreateInfo li{};
        li.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        li.setLayoutCount = (uint32_t)layouts.size();
        li.pSetLayouts = layouts.data();
        li.pushConstantRangeCount = 1;
        li.pPushConstantRanges = &pushRange;
        vkCreatePipelineLayout(device.device(), &li, nullptr, &samplePL);

        samplePipeline = std::make_unique<BurnhopePipeline>(
            device, "shaders/irradiance_sample.comp.spv", samplePL);
    }
}

void RadianceCascadesSystem::createDescriptorSets(VkImageView lightingImageView, VkSampler lightingSampler) {
    // Lighting read descriptor info
    VkDescriptorImageInfo lightInfo{};
    lightInfo.imageView   = lightingImageView;
    lightInfo.sampler     = lightingSampler;
    lightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    // probe write sets (один на каскад)
    for (int c = 0; c < RCConfig::CASCADE_COUNT; c++) {
        VkDescriptorImageInfo probeWriteInfo{};
        probeWriteInfo.imageView   = probeTex[c]->getImageView();
        probeWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        BurnhopeDescriptorWriter(*probeWriteLayout, pool)
            .writeImage(0, &probeWriteInfo)
            .writeImage(1, &lightInfo)
            .build(probeWriteSets[c]);
    }

    // merge sets: каждый каскад читает следующий
    // каскад CASCADE_COUNT-1 читает сам себя (последний, дальше некуда)
    for (int c = 0; c < RCConfig::CASCADE_COUNT; c++) {
        int nextC = std::min(c + 1, RCConfig::CASCADE_COUNT - 1);

        VkDescriptorImageInfo writeInfo{};
        writeInfo.imageView   = probeTex[c]->getImageView();
        writeInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo readInfo{};
        readInfo.imageView   = probeTex[nextC]->getImageView();
        readInfo.sampler     = probeTex[nextC]->getSampler();
        readInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        BurnhopeDescriptorWriter(*mergeLayout, pool)
            .writeImage(0, &writeInfo)
            .writeImage(1, &readInfo)
            .build(mergeReadSets[c]);
    }

    // irradiance write set
    {
        VkDescriptorImageInfo irWriteInfo{};
        irWriteInfo.imageView   = irradianceTex->getImageView();
        irWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo cascade0ReadInfo{};
        cascade0ReadInfo.imageView   = probeTex[0]->getImageView();
        cascade0ReadInfo.sampler     = probeTex[0]->getSampler();
        cascade0ReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        BurnhopeDescriptorWriter(*sampleWriteLayout, pool)
            .writeImage(0, &irWriteInfo)
            .writeImage(1, &cascade0ReadInfo)
            .build(irradianceWriteSet);
    }

    // irradiance read set (для lighting.comp)
    // Создаём отдельный layout с одним sampler2D
    auto irReadLayout = BurnhopeDescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    VkDescriptorImageInfo irReadInfo{};
    irReadInfo.imageView   = irradianceTex->getImageView();
    irReadInfo.sampler     = irradianceTex->getSampler();
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
    b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout           = oldLayout;
    b.newLayout           = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image               = image;
    b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    b.srcAccessMask       = src;
    b.dstAccessMask       = dst;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

void RadianceCascadesSystem::dispatch(
    VkCommandBuffer cmd,
    VkDescriptorSet globalSet,
    VkDescriptorSet gBufferSet,
    const glm::mat4& invViewProj,
    const glm::vec3& cameraPos,
    const glm::vec3& sceneMin,
    const glm::vec3& sceneMax,
    VkExtent2D extent)
{
    // ========================================================
    // PASS 1: Обновляем зонды всех каскадов (от дальнего к ближнему)
    // ========================================================
    probeUpdatePipeline->bindCompute(cmd);

    for (int c = RCConfig::CASCADE_COUNT - 1; c >= 0; c--) {
        RCPushConstants push{};
        push.invViewProj  = invViewProj;
        push.cameraPos    = glm::vec4(cameraPos, 1.0f);
        push.probeGridMin = glm::vec4(sceneMin, 0.0f);
        push.probeGridMax = glm::vec4(sceneMax, 0.0f);
        push.probeCount   = glm::ivec4(cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c), c);
        push.rayLength    = RCConfig::BASE_RAY_LENGTH * std::pow(2.0f, (float)c);
        push.cascadeIndex = (float)c;
        push.screenWidth  = (float)extent.width;
        push.screenHeight = (float)extent.height;

        vkCmdPushConstants(cmd, probeUpdatePL, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(RCPushConstants), &push);

        std::vector<VkDescriptorSet> sets = { globalSet, gBufferSet, probeWriteSets[c] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                probeUpdatePL, 0, (uint32_t)sets.size(), sets.data(), 0, nullptr);

        int px = cascadeProbeX(c);
        int pz = cascadeProbeZ(c);
        uint32_t w = px * RCConfig::OCTA_SIZE;
        uint32_t h = cascadeProbeY(c) * pz * RCConfig::OCTA_SIZE;

        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        // Барьер между каскадами
        insertBarrier(cmd, probeTex[c]->getImage(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    // ========================================================
    // PASS 2: Слияние каскадов (от дальнего к ближнему)
    // ========================================================
    mergePipeline->bindCompute(cmd);

    for (int c = RCConfig::CASCADE_COUNT - 2; c >= 0; c--) {
        RCPushConstants push{};
        push.probeCount   = glm::ivec4(cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c), c);
        push.cascadeIndex = (float)c;

        vkCmdPushConstants(cmd, mergePL, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(RCPushConstants), &push);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                mergePL, 0, 1, &mergeReadSets[c], 0, nullptr);

        uint32_t w = cascadeProbeX(c) * RCConfig::OCTA_SIZE;
        uint32_t h = cascadeProbeY(c) * cascadeProbeZ(c) * RCConfig::OCTA_SIZE;

        vkCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

        insertBarrier(cmd, probeTex[c]->getImage(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    // ========================================================
    // PASS 3: Сэмплируем irradiance для каждого пикселя
    // ========================================================
    samplePipeline->bindCompute(cmd);

    RCPushConstants push{};
    push.invViewProj  = invViewProj;
    push.cameraPos    = glm::vec4(cameraPos, 1.0f);
    push.probeGridMin = glm::vec4(sceneMin, 0.0f);
    push.probeGridMax = glm::vec4(sceneMax, 0.0f);
    push.probeCount   = glm::ivec4(cascadeProbeX(0), cascadeProbeY(0), cascadeProbeZ(0), 0);
    push.screenWidth  = (float)extent.width;
    push.screenHeight = (float)extent.height;

    vkCmdPushConstants(cmd, samplePL, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(RCPushConstants), &push);

    std::vector<VkDescriptorSet> sets = { globalSet, gBufferSet, irradianceWriteSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            samplePL, 0, (uint32_t)sets.size(), sets.data(), 0, nullptr);

    vkCmdDispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);
}

void RadianceCascadesSystem::rebuildOnResize(VkExtent2D newExtent,
                                              VkImageView lightingImageView,
                                              VkSampler   lightingSampler) {
    screenExtent = newExtent;
    vkDeviceWaitIdle(device.device());
    createIrradianceTexture();
    createDescriptorSets(lightingImageView, lightingSampler);
}

} // namespace burnhope