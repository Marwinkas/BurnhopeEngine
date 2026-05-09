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
        VkSampler lightingSampler,
        VkDescriptorSetLayout rtLayout,
        VkDescriptorSetLayout storageLayout,
        VkDescriptorSetLayout textureLayout)
        : device(device), pool(pool), screenExtent(screenExtent),
          globalLayoutRef(globalLayout), gBufferLayoutRef(gBufferLayout)
    {
        createProbeTextures();
        createGITextures();
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
            barrier.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image            = probeTex[c]->getImage();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask    = 0;
            barrier.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            device.endSingleTimeCommands(cmd);
        }
    }

    void RadianceCascadesSystem::createGITextures()
    {
        diffuseGITex = std::make_unique<BurnhopeTexture>(
            device,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{screenExtent.width, screenExtent.height, 1},
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);

        specularGITex = std::make_unique<BurnhopeTexture>(
            device,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VkExtent3D{screenExtent.width, screenExtent.height, 1},
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);

        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        
        std::array<VkImageMemoryBarrier, 2> barriers{};
        for(int i = 0; i < 2; i++) {
            barriers[i].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[i].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[i].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[i].image            = i == 0 ? diffuseGITex->getImage() : specularGITex->getImage();
            barriers[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barriers[i].srcAccessMask    = 0;
            barriers[i].dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        }
        
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers.data());
        device.endSingleTimeCommands(cmd);
    }

    void RadianceCascadesSystem::createLayouts()
    {
        // Set(2) для probe_update.comp:
        //   binding 0 → outProbe    (storage image, write)
        //   binding 1 → inLighting  (sampler2D, read) — не используется активно, но пусть будет
        probeWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        // Set(0) для cascade_merge.comp:
        //   binding 0 → currentCascade (storage image, read+write)
        //   binding 1 → nextCascade    (sampler2D, read)
        mergeLayout = BurnhopeDescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();

        // Set(2) для gi_sample.comp:
        //   binding 0 → outDiffuseGI  (storage image, write)
        //   binding 1 → outSpecularGI (storage image, write)
        //   binding 2 → cascade0      (sampler2D, read)
        sampleWriteLayout = BurnhopeDescriptorSetLayout::Builder(device)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          VK_SHADER_STAGE_COMPUTE_BIT)
            .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
            .build();
    }

    void RadianceCascadesSystem::createPipelines(
        VkDescriptorSetLayout rtLayout,
        VkDescriptorSetLayout storageLayout,
        VkDescriptorSetLayout textureLayout)
    {
        // probe_update.comp layouts:
        // set 0 → globalLayout
        // set 1 → gBufferLayout
        // set 2 → probeWriteLayout   (outProbe image + inLighting sampler)
        // set 3 → rtLayout           (TLAS)
        // set 4 → storageLayout      (ObjectBuffer + MaterialBuffer)
        // set 5 → textureLayout      (bindlessTextures)
        std::vector<VkDescriptorSetLayout> updateLayouts = {
            globalLayoutRef,
            gBufferLayoutRef,
            probeWriteLayout->getDescriptorSetLayout(),
            rtLayout,
            storageLayout,
            textureLayout
        };
        probeUpdateShader = std::make_unique<ComputeShader>(
            device, "shaders/probe_update.comp.spv",
            updateLayouts, sizeof(RCPushConstants));

        // cascade_merge.comp layouts:
        // set 0 → mergeLayout  (currentCascade storage image + nextCascade sampler)
        std::vector<VkDescriptorSetLayout> mergeLayouts = {
            mergeLayout->getDescriptorSetLayout()
        };
        mergeShader = std::make_unique<ComputeShader>(
            device, "shaders/cascade_merge.comp.spv",
            mergeLayouts, sizeof(RCPushConstants));

        // irradiance_sample.comp layouts:
        // set 0 → globalLayout
        // set 1 → gBufferLayout
        // set 2 → sampleWriteLayout  (outIrradiance image + cascade0 sampler)
        std::vector<VkDescriptorSetLayout> sampleLayouts = {
            globalLayoutRef,
            gBufferLayoutRef,
            sampleWriteLayout->getDescriptorSetLayout()
        };
        sampleShader = std::make_unique<ComputeShader>(
            device, "shaders/gi_sample.comp.spv", // Изменено имя шейдера
            sampleLayouts, sizeof(RCPushConstants));
    }

    void RadianceCascadesSystem::createDescriptorSets(
        VkImageView lightingImageView,
        VkSampler   lightingSampler)
    {
        VkDescriptorImageInfo lightInfo{};
        lightInfo.imageView   = lightingImageView;
        lightInfo.sampler     = lightingSampler;
            lightInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Исправлено: текстура остается в General с прошлого кадра

        // --- probeWriteSets[c]: set 2 в probe_update.comp ---
        // binding 0 → probeTex[c] (write, GENERAL)
        // binding 1 → inLighting  (read, SHADER_READ_ONLY_OPTIMAL)
        for (int c = 0; c < RCConfig::CASCADE_COUNT; c++)
        {
            VkDescriptorImageInfo probeWriteInfo{};
            probeWriteInfo.imageView   = probeTex[c]->getImageView();
            probeWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            BurnhopeDescriptorWriter(*probeWriteLayout, pool)
                .writeImage(0, &probeWriteInfo)
                .writeImage(1, &lightInfo)
                .build(probeWriteSets[c]);
        }

        // --- mergeReadSets[c]: set 0 в cascade_merge.comp ---
        // binding 0 → probeTex[c]     (currentCascade, storage image read+write)
        // binding 1 → probeTex[c+1]   (nextCascade, sampler2D read)
        //
        // ВАЖНО: последний каскад (CASCADE_COUNT-1) не мержится,
        // поэтому mergeReadSets[CASCADE_COUNT-1] не используется.
        // Для c = 0..CASCADE_COUNT-2: nextC = c+1 (всегда валидный индекс)
        for (int c = 0; c < RCConfig::CASCADE_COUNT; c++)
        {
            // nextC: для последнего каскада ставим его же (set не используется)
            int nextC = (c + 1 < RCConfig::CASCADE_COUNT) ? c + 1 : c;

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

        // --- giWriteSet: set 2 в gi_sample.comp ---
        // binding 0 → diffuseGITex  (outDiffuseGI, storage image write)
        // binding 1 → specularGITex (outSpecularGI, storage image write)
        // binding 2 → probeTex[0]   (cascade0, sampler2D read)
        {
            VkDescriptorImageInfo diffWriteInfo{};
            diffWriteInfo.imageView   = diffuseGITex->getImageView();
            diffWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo specWriteInfo{};
            specWriteInfo.imageView   = specularGITex->getImageView();
            specWriteInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo cascade0ReadInfo{};
            cascade0ReadInfo.imageView   = probeTex[0]->getImageView();
            cascade0ReadInfo.sampler     = probeTex[0]->getSampler();
            cascade0ReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            BurnhopeDescriptorWriter(*sampleWriteLayout, pool)
                .writeImage(0, &diffWriteInfo)
                .writeImage(1, &specWriteInfo)
                .writeImage(2, &cascade0ReadInfo)
                .build(giWriteSet);
        }

        // --- giSet: для финального lighting шейдера ---
        // binding 0 → diffuseGITex (sampler2D read)
        // binding 1 → specularGITex (sampler2D read)
        {
            auto irReadLayout = BurnhopeDescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
                .build();

            VkDescriptorImageInfo diffReadInfo{};
            diffReadInfo.imageView   = diffuseGITex->getImageView();
            diffReadInfo.sampler     = diffuseGITex->getSampler();
            diffReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            VkDescriptorImageInfo specReadInfo{};
            specReadInfo.imageView   = specularGITex->getImageView();
            specReadInfo.sampler     = specularGITex->getSampler();
            specReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            
            VkDescriptorImageInfo cascade0ReadInfo{};
            cascade0ReadInfo.imageView   = probeTex[0]->getImageView();
            cascade0ReadInfo.sampler     = probeTex[0]->getSampler();
            cascade0ReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            BurnhopeDescriptorWriter(*irReadLayout, pool)
                .writeImage(0, &diffReadInfo)
                .writeImage(1, &specReadInfo)
                .writeImage(2, &cascade0ReadInfo)
                .build(giSet);
        }
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
        b.subresourceRange    = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask       = src;
        b.dstAccessMask       = dst;
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
        VkDescriptorSet storageSet,
        VkDescriptorSet textureSet)
    {
        // =====================================================================
        // PASS 1: Probe Update (от дальних каскадов к ближним)
        // =====================================================================
        probeUpdateShader->bind(cmd);

        for (int c = RCConfig::CASCADE_COUNT - 1; c >= 0; c--)
        {
            RCPushConstants push{};
            push.probeGridMin = glm::vec4(sceneMin, 0.0f);
            push.probeGridMax = glm::vec4(sceneMax, 0.0f);
            push.probeCount   = glm::ivec4(
                cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c),
                c  // .w = индекс каскада (используется в шейдере для неба и startOffset)
            );
            push.params = glm::vec4(
                RCConfig::BASE_RAY_LENGTH * std::pow(2.0f, (float)c), // x: длина луча
                (c == RCConfig::CASCADE_COUNT - 1) ? 1.0f : 0.0f,     // y: 1.0 если это последний каскад
                (float)extent.height,                                   // z: не используется в probe шейдере
                (float)RCConfig::OCTA_SIZE                              // w: размер октаэдра тайла
            );

            probeUpdateShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
            probeUpdateShader->bindDescriptorSets(cmd, {
                globalSet,          // set 0
                gBufferSet,         // set 1
                probeWriteSets[c],  // set 2: outProbe(image) + inLighting(sampler)
                rtSet,              // set 3: TLAS
                storageSet,         // set 4: ObjectBuffer + MaterialBuffer
                textureSet          // set 5: bindlessTextures
            });

            uint32_t w = (uint32_t)(cascadeProbeX(c) * RCConfig::OCTA_SIZE);
            uint32_t h = (uint32_t)(cascadeProbeY(c) * cascadeProbeZ(c) * RCConfig::OCTA_SIZE);
            probeUpdateShader->dispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

            // Барьер: запись завершена, дальше — чтение в merge
            insertBarrier(cmd, probeTex[c]->getImage(),
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        // =====================================================================
        // PASS 2: Cascade Merge (от CASCADE_COUNT-2 до 0)
        // Каскад c читает из c (currentCascade) и c+1 (nextCascade),
        // пишет обратно в c.
        // =====================================================================
        mergeShader->bind(cmd);

        for (int c = RCConfig::CASCADE_COUNT - 2; c >= 0; c--)
        {
            RCPushConstants push{};
            push.probeGridMin = glm::vec4(sceneMin, 0.0f);   // нужны для позиций зондов в шейдере
            push.probeGridMax = glm::vec4(sceneMax, 0.0f);
            push.probeCount   = glm::ivec4(
                cascadeProbeX(c), cascadeProbeY(c), cascadeProbeZ(c),
                c  // .w = индекс текущего каскада
            );
            push.params = glm::vec4(
                RCConfig::BASE_RAY_LENGTH * std::pow(2.0f, (float)c), // x: длина луча (для reference, merge не трассирует)
                (float)extent.width,
                (float)extent.height,
                (float)RCConfig::OCTA_SIZE                             // w: размер октаэдра — КРИТИЧНО для адресации тайлов
            );

            mergeShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
            mergeShader->bindDescriptorSets(cmd, {
                mergeReadSets[c]   // set 0: currentCascade(image c) + nextCascade(sampler c+1)
            });

            uint32_t w = (uint32_t)(cascadeProbeX(c) * RCConfig::OCTA_SIZE);
            uint32_t h = (uint32_t)(cascadeProbeY(c) * cascadeProbeZ(c) * RCConfig::OCTA_SIZE);
            mergeShader->dispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);

            // Барьер: merge записал в c, следующая итерация читает c как nextCascade
            insertBarrier(cmd, probeTex[c]->getImage(),
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        // =====================================================================
        // PASS 3: GI Sample (Diffuse + Specular)
        // Читает probeTex[0] (смёрженный каскад 0), пишет в diffuseGITex и specularGITex.
        // =====================================================================
        sampleShader->bind(cmd);

        {
            RCPushConstants push{};
            push.probeGridMin = glm::vec4(sceneMin, 0.0f);
            push.probeGridMax = glm::vec4(sceneMax, 0.0f);
            push.probeCount   = glm::ivec4(
                cascadeProbeX(0), cascadeProbeY(0), cascadeProbeZ(0),
                0  // каскад 0
            );
            push.params = glm::vec4(
                RCConfig::BASE_RAY_LENGTH,  // x: длина луча каскада 0
                (float)extent.width,         // y: ширина экрана (для реконструкции позиции)
                (float)extent.height,        // z: высота экрана
                (float)RCConfig::OCTA_SIZE   // w: размер октаэдра
            );

            sampleShader->pushConstants(cmd, &push, sizeof(RCPushConstants));
            sampleShader->bindDescriptorSets(cmd, {
                globalSet,          // set 0: GlobalSceneUbo (invViewProj и т.д.)
                gBufferSet,         // set 1: gNormalRoughness, gAlbedo, gDepth
                giWriteSet          // set 2: outDiffuse, outSpecular + cascade0
            });

            sampleShader->dispatch(cmd,
                (extent.width  + 15) / 16,
                (extent.height + 15) / 16, 1);
        }

        // Финальный барьер: текстуры готовы для чтения в lighting шейдере
        insertBarrier(cmd, diffuseGITex->getImage(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            
        insertBarrier(cmd, specularGITex->getImage(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    void RadianceCascadesSystem::rebuildOnResize(
        VkExtent2D  newExtent,
        VkImageView lightingImageView,
        VkSampler   lightingSampler)
    {
        screenExtent = newExtent;
        vkDeviceWaitIdle(device.device());
        createGITextures();
        createDescriptorSets(lightingImageView, lightingSampler);
    }

} // namespace burnhope