#include "MainApp.hpp"
#include "Render/Camera.hpp"
#include "Render/SceneManager.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <chrono>
#include <stdexcept>
#include <array>
#include <thread>
#include "Render/RenderGraph.hpp"
namespace burnhope
{
    FirstApp::FirstApp()
    {
        globalPool = BurnhopeDescriptorPool::Builder(lveDevice)
                         .setMaxSets(100)
                         .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 50)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 50)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4000)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 10)
                         .build();
        defaultWhiteTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/white.png");
        defaultNormalTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/white.png");
        shadowSystem = std::make_unique<BurnhopeShadowSystem>(lveDevice);
        defaultWhiteMaterial = std::make_shared<Material>();
        defaultWhiteMaterial->setAlbedo(defaultWhiteTex);
        defaultWhiteMaterial->setNormal(defaultNormalTex);
        globalSetLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();

        portalUboBuffers.resize(MAX_PORTALS);
        portalDescriptorSets.resize(MAX_PORTALS);

        for (int p = 0; p < MAX_PORTALS; p++)
        {
            // Резервируем место под кадры (обычно 2 или 3)
            portalUboBuffers[p].resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
            portalDescriptorSets[p].resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);

            for (int f = 0; f < BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT; f++)
            {
                portalUboBuffers[p][f] = std::make_unique<BurnhopeBuffer>(
                    lveDevice,
                    sizeof(GlobalUbo),
                    1,
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
                portalUboBuffers[p][f]->map();

                auto bufferInfo = portalUboBuffers[p][f]->descriptorInfo();
                BurnhopeDescriptorWriter(*globalSetLayout, *globalPool)
                    .writeBuffer(0, &bufferInfo)
                    .build(portalDescriptorSets[p][f]);
            }
        }

        gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, lveWindow.getExtent());

        portalRenderSystem = std::make_unique<PortalRenderSystem>(
            lveDevice,
            gBuffer->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout());

        simpleRenderSystem = std::make_unique<GeometryRenderSystem>(
            lveDevice,
            gBuffer->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout());

        initCompute(globalSetLayout->getDescriptorSetLayout());

        loadGameObjects(registry);
    }
    FirstApp::~FirstApp()
    {
        vkDeviceWaitIdle(lveDevice.device());
        cullingSystem.reset();
        lightingSystem.reset();
        shadowSystem.reset();
        lightUboBuffer.reset();
        gBuffer.reset();
        hdrOutputTexture.reset();
        objectBuffer.reset();
        materialBuffer.reset();
        faceMatricesBuffer.reset();
        globalPool.reset();
        gBufferLayoutPtr.reset();
        outputLayoutPtr.reset();
        shadowLayoutPtr.reset();
        lightLayoutPtr.reset();
        dummyGridBuffer.reset();
        dummyIndexBuffer.reset();
        for (auto &pBuffers : portalUboBuffers)
        {
            for (auto &buffer : pBuffers)
            {
                buffer.reset();
            }
        }
        portalUboBuffers.clear();
        portalDescriptorSets.clear();
    }
    glm::mat4 shadowPerspective(float fovY, float aspect, float zNear, float zFar)
    {
        glm::mat4 proj = glm::perspective(glm::radians(fovY), aspect, zNear, zFar);
        proj[1][1] *= -1.0f;
        return proj;
    }
    glm::mat4 computeObliqueProjection(const glm::mat4 &proj,
                                       const glm::mat4 &view,
                                       const glm::vec3 &planePos,
                                       const glm::vec3 &planeNormal)
    {
        glm::vec3 normalView = glm::normalize(
            glm::mat3(glm::transpose(glm::inverse(view))) * planeNormal);
        glm::vec3 pointView = glm::vec3(view * glm::vec4(planePos, 1.0f));
        float d = -glm::dot(normalView, pointView);
        glm::vec4 clipPlane(normalView, d);

        if (clipPlane.z > 0.0f)
            clipPlane = -clipPlane;

        glm::mat4 result = proj;

        glm::vec4 q;
        q.x = (glm::sign(clipPlane.x) + proj[2][0]) / proj[0][0];
        q.y = (glm::sign(clipPlane.y) + proj[2][1]) / proj[1][1];
        q.z = -1.0f;
        q.w = (1.0f + proj[2][2]) / proj[3][2];

        float dotProd = glm::dot(clipPlane, q);
        if (glm::abs(dotProd) < 1e-6f)
            return proj;
        glm::vec4 c = clipPlane / dotProd;

        result[0][2] = c.x;
        result[1][2] = c.y;
        result[2][2] = c.z;
        result[3][2] = c.w;

        return result;
    }
    void FirstApp::RebuildBatches(entt::registry &registry, GeometryRenderSystem &renderSystem)
    {
        std::vector<ObjectData> objDataList;
        std::vector<MaterialData> matDataList;
        std::vector<VkDescriptorImageInfo> textureInfos;
        std::map<Material *, uint32_t> matToIndex;
        std::map<BurnhopeTexture *, uint32_t> texToIndex;
        uint32_t globalMatIndex = 0;
        uint32_t globalTexIndex = 0;
        textureInfos.push_back(defaultWhiteTex->getImageInfo());
        uint32_t defaultWhiteIdx = globalTexIndex++;
        textureInfos.push_back(defaultNormalTex->getImageInfo());
        uint32_t defaultNormalIdx = globalTexIndex++;
        auto getTexIndex = [&](std::shared_ptr<BurnhopeTexture> tex, uint32_t defaultIdx) -> uint32_t
        {
            if (!tex)
                return defaultIdx;
            if (texToIndex.find(tex.get()) == texToIndex.end())
            {
                texToIndex[tex.get()] = globalTexIndex;
                textureInfos.push_back(tex->getImageInfo());
                return globalTexIndex++;
            }
            return texToIndex[tex.get()];
        };
        auto view = registry.view<TransformComponent, MeshComponent>();
        for (auto [entity, transformComp, meshComp] : view.each())
        {
            if (!meshComp.model || !meshComp.isVisible)
                continue;
            const auto &subMeshes = meshComp.model->getSubMeshes();
            for (uint32_t i = 0; i < subMeshes.size(); i++)
            {
                uint32_t matIdx = subMeshes[i].materialIndex;
                std::shared_ptr<Material> currentMat = (matIdx < meshComp.materials.size())
                                                           ? meshComp.materials[matIdx]
                                                           : defaultWhiteMaterial;
                uint32_t currentMatID = 0;
                if (matToIndex.find(currentMat.get()) == matToIndex.end())
                {
                    currentMatID = globalMatIndex++;
                    matToIndex[currentMat.get()] = currentMatID;
                    MaterialData matData{};
                    matData.uvScale = currentMat->uvScale;
                    matData.hasAlbedo = currentMat->hasAlbedo ? 1 : 0;
                    matData.hasNormal = currentMat->hasNormal ? 1 : 0;
                    matData.hasRoughness = currentMat->hasRoughness ? 1 : 0;
                    matData.hasMetallic = currentMat->hasMetallic ? 1 : 0;
                    matData.hasAO = currentMat->hasAO ? 1 : 0;
                    matData.useORM = currentMat->isORM ? 1 : 0;
                    matData.albedoIdx = getTexIndex(currentMat->albedoMap, defaultWhiteIdx);
                    matData.normalIdx = getTexIndex(currentMat->normalMap, defaultNormalIdx);
                    matData.roughnessIdx = getTexIndex(currentMat->roughnessMap, defaultWhiteIdx);
                    matData.metallicIdx = getTexIndex(currentMat->metallicMap, defaultWhiteIdx);
                    matData.aoIdx = getTexIndex(currentMat->aoMap, defaultWhiteIdx);
                    matDataList.push_back(matData);
                }
                else
                {
                    currentMatID = matToIndex[currentMat.get()];
                }
                ObjectData obj{};
                obj.modelMatrix = transformComp.transform.matrix;
                obj.materialID = currentMatID;
                obj.vertexBufferAddress = meshComp.model->getVertexBufferAddress();
                obj.indexBufferAddress = meshComp.model->getIndexBufferAddress();
                objDataList.push_back(obj);
            }
        }
        totalSubMeshCount = static_cast<uint32_t>(objDataList.size());
        if (!cullingSystem)
        {
            cullingSystem = std::make_unique<CullingSystem>(lveDevice, totalSubMeshCount);
        }
        if (!objDataList.empty())
        {
            objectBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(ObjectData), objDataList.size(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            objectBuffer->map();
            objectBuffer->writeToBuffer(objDataList.data());
            cullingSystem->bindObjectBuffer(
                objectBuffer->getBuffer(),
                sizeof(ObjectData) * objDataList.size());
        }
        if (!matDataList.empty())
        {
            materialBuffer = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(MaterialData), matDataList.size(),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            materialBuffer->map();
            materialBuffer->writeToBuffer(matDataList.data());
        }
        if (objectBuffer && materialBuffer)
        {
            auto objInfo = objectBuffer->descriptorInfo();
            auto matInfo = materialBuffer->descriptorInfo();
            BurnhopeDescriptorWriter(*renderSystem.getRenderSystemLayout(), *globalPool)
                .writeBuffer(0, &objInfo)
                .writeBuffer(1, &matInfo)
                .build(storageSet);
        }
        std::vector<SubMeshGPUInfo> subMeshInfos;
        auto view2 = registry.view<TransformComponent, MeshComponent>();
        for (auto [entity, transformComp, meshComp] : view2.each())
        {
            if (!meshComp.model || !meshComp.isVisible)
                continue;
            for (const auto &sub : meshComp.model->getSubMeshes())
            {
                SubMeshGPUInfo info{};
                info.aabbMin = sub.aabbMin;
                info.aabbMax = sub.aabbMax;
                info.boundingRadius = sub.boundingRadius;
                for (int j = 0; j < 4; j++)
                {
                    info.indexCounts[j] = sub.indexCounts[j];
                    info.firstIndices[j] = sub.firstIndices[j];
                }
                info.lodCount = sub.lodCount;
                info.materialIndex = sub.materialIndex;
                info.pad1 = info.pad2 = 0;
                subMeshInfos.push_back(info);
            }
        }
        cullingSystem->uploadSubMeshData(subMeshInfos);
        if (hizSystem)
        {
            cullingSystem->updateHiZDescriptor(hizSystem->getHiZImageInfo());
        }
        if (!textureInfos.empty())
        {
            BurnhopeDescriptorWriter(*renderSystem.getTextureLayout(), *globalPool)
                .writeImageArray(0, textureInfos)
                .build(textureSet);
        }
    }
    void FirstApp::run()
    {

        std::vector<std::unique_ptr<BurnhopeBuffer>> uboBuffers(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < uboBuffers.size(); i++)
        {
            uboBuffers[i] = std::make_unique<BurnhopeBuffer>(lveDevice, sizeof(GlobalUbo), 1, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            uboBuffers[i]->map();
        }

        std::vector<VkDescriptorSet> globalDescriptorSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++)
        {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            BurnhopeDescriptorWriter(*globalSetLayout, *globalPool).writeBuffer(0, &bufferInfo).build(globalDescriptorSets[i]);
        }

        // std::vector<std::unique_ptr<BurnhopeBuffer>> portalUboBuffers(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        // for (int i = 0; i < (int)portalUboBuffers.size(); i++)
        //{
        //      portalUboBuffers[i] = std::make_unique<BurnhopeBuffer>(
        //          lveDevice, sizeof(GlobalUbo), 1,
        //          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        //          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        //      portalUboBuffers[i]->map();
        //
        //
        // std::vector<VkDescriptorSet> portalDescriptorSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        // for (int i = 0; i < (int)portalDescriptorSets.size(); i++)
        //{
        //     auto bufferInfo = portalUboBuffers[i]->descriptorInfo();
        //     BurnhopeDescriptorWriter(*globalSetLayout, *globalPool)
        //         .writeBuffer(0, &bufferInfo)
        //         .build(portalDescriptorSets[i]);
        // }

        RebuildBatches(registry, *simpleRenderSystem);
        buildTLAS(registry);

        VkWriteDescriptorSetAccelerationStructureKHR asInfo{};
        asInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        asInfo.accelerationStructureCount = 1;
        asInfo.pAccelerationStructures = &tlasHandle;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = rtSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pNext = &asInfo;

        vkUpdateDescriptorSets(lveDevice.device(), 1, &descriptorWrite, 0, nullptr);

        shadowObjectLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice).addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT).build();
        shadowRenderSystem = std::make_unique<ShadowRenderSystem>(lveDevice, shadowSystem->getCSM()->getRenderPass(), shadowObjectLayoutPtr->getDescriptorSetLayout());
        if (objectBuffer)
        {
            auto objInfo = objectBuffer->descriptorInfo();
            BurnhopeDescriptorWriter(*shadowObjectLayoutPtr, *globalPool).writeBuffer(0, &objInfo).build(shadowObjectSet);
        }
        Camera camera(WIDTH, HEIGHT, glm::vec3(0.0f, 0.0f, 0.0f));
        auto currentTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        auto fpsTimer = currentTime;
        RenderPipeline renderPipeline;
        const double targetFPS = 60.0;
        const double maxPeriod = 1.0 / targetFPS;

        while (!lveWindow.shouldClose())
        {
            glfwPollEvents();

            bool transformsChanged = false;

            registry.view<TransformComponent>().each([&](TransformComponent &tComp)
                                                     {
                if (tComp.transform.updatematrix) {
                    tComp.transform.updateMatrixIfNeeded();
                    transformsChanged = true;
                } });

            if (transformsChanged && objectBuffer)
            {
                auto *mappedData = reinterpret_cast<ObjectData *>(objectBuffer->getMappedMemory());

                if (mappedData)
                {
                    uint32_t objIndex = 0;
                    auto meshView = registry.view<TransformComponent, MeshComponent>();

                    for (auto [entity, transformComp, meshComp] : meshView.each())
                    {
                        if (!meshComp.model || !meshComp.isVisible)
                            continue;

                        glm::mat4 worldMatrix = transformComp.transform.matrix;

                        for (uint32_t i = 0; i < meshComp.model->getSubMeshes().size(); i++)
                        {
                            mappedData[objIndex].modelMatrix = worldMatrix;
                            objIndex++;
                        }
                    }
                }
            }

            auto extent = lveWindow.getExtent();
            while (extent.width == 0 || extent.height == 0)
            {
                extent = lveWindow.getExtent();
                glfwWaitEvents();
            }
            camera.width = extent.width;
            camera.height = extent.height;
            VkExtent2D swapExtent = lveRenderer.getSwapChainExtent();
            if (extent.width != swapExtent.width || extent.height != swapExtent.height)
            {
                vkDeviceWaitIdle(lveDevice.device());
                lveRenderer.recreateSwapChain();
                VkExtent2D newExtent = lveRenderer.getSwapChainExtent();
                gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, newExtent);
                hdrOutputTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                     VkExtent3D{newExtent.width, newExtent.height, 1},
                                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                                     VK_SAMPLE_COUNT_1_BIT);
                rebuildGBufferDescriptorSets();
                if (rcSystem)
                {
                    rcSystem->rebuildOnResize(newExtent, hdrOutputTexture->getImageView(), hdrOutputTexture->getSampler());
                }
                continue;
            }
            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();

            if (frameTime < maxPeriod)
            {
                double sleepTime = maxPeriod - frameTime;
                std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));

                // Пересчитываем время после сна для честного deltaTime
                newTime = std::chrono::high_resolution_clock::now();
                frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            }

            currentTime = newTime;
            frameCount++;
            if (std::chrono::duration<float>(newTime - fpsTimer).count() >= 1.0f)
            {
                std::cout << "FPS: " << frameCount << "\n";
                frameCount = 0;
                fpsTimer = newTime;
            }
            camera.Inputs(lveWindow.getGLFWwindow(), frameTime);
            if (auto commandBuffer = lveRenderer.beginFrame())
            {
                int frameIndex = lveRenderer.getFrameIndex();
                FrameInfo frameInfo{frameIndex, frameTime, commandBuffer, camera, globalDescriptorSets[frameIndex], *globalPool};
                shadowSystem->updateLights(registry, camera.Position);
                {
                    const auto &uboData = shadowSystem->getLightUBO();
                    lightUboBuffer->writeToBuffer((void *)&uboData);
                    lightUboBuffer->flush();
                }
                faceMatricesBuffer->writeToBuffer(const_cast<PointFaceMatrices *>(shadowSystem->getFaceMatricesData()));
                faceMatricesBuffer->flush();
                GlobalUbo ubo{};
                ubo.projection = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);
                ubo.view = camera.GetViewMatrix();
                ubo.invViewProj = glm::inverse(ubo.projection * ubo.view);
                ubo.camPos = camera.Position;
                ubo.zNear = 0.1f;
                ubo.zFar = 1000.0f;
                ubo.screenSize = glm::vec4(extent.width, extent.height, 0.f, 0.f);
                ubo.sunDir = shadowSystem->getSunDir();

                ubo.sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
                ubo.sunIntensity = 5.0f;

                auto lightView = registry.view<LightComponent>();
                for (auto entity : lightView)
                {
                    auto &light = lightView.get<LightComponent>(entity).light;
                    if (light.enable && light.type == LightType::Directional)
                    {
                        ubo.sunColor = light.color;
                        ubo.sunIntensity = light.intensity;
                        break;
                    }
                }
                ubo.lightSize = 1.0f;
                auto cascadeMats = shadowSystem->getCSM()->calculateMatrices(
                    camera, shadowSystem->getSunDir(),
                    {shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]});
                for (int i = 0; i < 4; i++)
                    ubo.sunLightSpaceMatrices[i] = cascadeMats[i];
                ubo.cascadeSplits = glm::vec4(shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();
                renderPipeline.clear();
                renderPipeline.addPass("Shadow Maps Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    for (int i = 0; i < BurnhopeCSM::CASCADE_COUNT; i++) {
                        VkRenderPassBeginInfo rpInfo{};
                        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rpInfo.renderPass = shadowSystem->getCSM()->getRenderPass();
                        rpInfo.framebuffer = shadowSystem->getCSM()->getFramebuffer(i);
                        rpInfo.renderArea.extent = { BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE };
                        VkClearValue clearVal{}; clearVal.depthStencil = { 1.0f, 0 };
                        rpInfo.clearValueCount = 1; rpInfo.pClearValues = &clearVal;
                        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
                        VkViewport vp{}; vp.width = vp.height = (float)BurnhopeCSM::SHADOW_MAP_SIZE; vp.maxDepth = 1.0f;
                        vkCmdSetViewport(cmd, 0, 1, &vp);
                        VkRect2D sc{ {0,0}, {BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE} };
                        vkCmdSetScissor(cmd, 0, 1, &sc);
                        shadowRenderSystem->renderShadow(cmd, cascadeMats[i], *cullingSystem, registry, shadowObjectSet);
                        vkCmdEndRenderPass(cmd);
                    }
                    VkRenderPassBeginInfo clearPass{};
                    clearPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; clearPass.renderPass = shadowSystem->getAtlas()->getRenderPass();
                    clearPass.framebuffer = shadowSystem->getAtlas()->getFramebuffer();
                    clearPass.renderArea.extent = { BurnhopeShadowAtlas::ATLAS_RESOLUTION, BurnhopeShadowAtlas::ATLAS_RESOLUTION };
                    VkClearValue clearVal{}; clearVal.depthStencil = { 1.0f, 0 };
                    clearPass.clearValueCount = 1; clearPass.pClearValues = &clearVal;
                    vkCmdBeginRenderPass(cmd, &clearPass, VK_SUBPASS_CONTENTS_INLINE);
                    auto lightView = registry.view<LightComponent, TransformComponent>();
                    for (auto entity : lightView) {
                        auto& light = lightView.get<LightComponent>(entity).light;
                        auto& trans = lightView.get<TransformComponent>(entity).transform;
                        if (!light.enable || !light.castShadows || light.type == LightType::Directional || light.shadowSlot < 0) continue;
                        int tileSize = light.shadowTileSize;
                        int pxX = (light.shadowSlot % BurnhopeShadowAtlas::ATLAS_IN_UNITS) * BurnhopeShadowAtlas::MIN_TILE;
                        int pxY = (light.shadowSlot / BurnhopeShadowAtlas::ATLAS_IN_UNITS) * BurnhopeShadowAtlas::MIN_TILE;
                        if (light.type == LightType::Spot) {
                            shadowSystem->getAtlas()->setTileViewport(cmd, pxX, pxY, tileSize);
                            shadowRenderSystem->renderShadow(cmd, light.lightSpaceMatrix, *cullingSystem, registry, shadowObjectSet);
                        }
                        else if (light.type == LightType::Point) {
                            glm::vec3 pos = trans.position;
                            glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
                            const glm::vec3 dirs[6] = {
                                {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
                            };
                            const glm::vec3 ups[6] = {
                                {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
                            };  
                            for (int face = 0; face < 6; face++) {
                                int fx = pxX + face * tileSize;
                                int fy = pxY;
                                if (fx + tileSize > BurnhopeShadowAtlas::ATLAS_RESOLUTION) {
                                    fx = fx % BurnhopeShadowAtlas::ATLAS_RESOLUTION; fy += tileSize;
                                }
                                    glm::mat4 faceProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
                                    glm::mat4 faceView = glm::lookAt(pos, pos + dirs[face], ups[face]);
                                    glm::mat4 faceMatrix = faceProj * faceView;
                                shadowSystem->getAtlas()->setTileViewport(cmd, fx, fy, tileSize);
                                shadowRenderSystem->renderShadow(cmd, faceMatrix, *cullingSystem, registry, shadowObjectSet);
                            }
                        }
                    }
                    vkCmdEndRenderPass(cmd); });
                renderPipeline.addPass("Frustum Culling", [&](VkCommandBuffer cmd)
                                       {
                    auto vp = ubo.projection * ubo.view;
                    auto planes = CullingSystem::extractFrustumPlanes(vp);
                    cullingSystem->dispatchCulling(cmd, vp, ubo.camPos, planes, totalSubMeshCount); });
                renderPipeline.addPass("G-Buffer Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    VkRenderPassBeginInfo renderPassInfo{};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                    renderPassInfo.renderPass = gBuffer->getRenderPass();
                    renderPassInfo.framebuffer = gBuffer->getFramebuffer();
                    renderPassInfo.renderArea.extent = extent;
                    std::array<VkClearValue, 4> clearValues{};
                    clearValues[3].depthStencil = {1.0f, 0};
                    renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
                    renderPassInfo.pClearValues = clearValues.data();

                    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                    VkViewport viewport{};
                    viewport.width = (float)extent.width;
                    viewport.height = (float)extent.height;
                    viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{{0, 0}, extent};
                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                    simpleRenderSystem->renderEntities(frameInfo, registry, storageSet, textureSet,
                                                       *cullingSystem, totalSubMeshCount, false);

                    {
                        uint32_t idx = 0;
                        for (auto entity : registry.view<PortalComponent, TransformComponent>()) {
                            if (idx >= MAX_PORTALS) break;
                            auto& transform = registry.get<TransformComponent>(entity);
                            portalRenderSystem->drawMask(cmd, globalDescriptorSets[frameIndex],
                                                         transform.transform.matrix, idx + 1);
                            idx++;
                        }
                    }

                    {
                        uint32_t portalCounter = 0;
                        for (auto entity : registry.view<PortalComponent, TransformComponent>()) {
                            if (portalCounter >= MAX_PORTALS) break;
                            auto& portal    = registry.get<PortalComponent>(entity);
                            auto& transform = registry.get<TransformComponent>(entity);
                        
                            if (portal.targetPortal == entt::null) { portalCounter++; continue; }
                        
                            portalRenderSystem->drawDepthReset(cmd, globalDescriptorSets[frameIndex],
                                                               transform.transform.matrix, portalCounter + 1);
                            
                            auto& targetTransform = registry.get<TransformComponent>(portal.targetPortal);
                            glm::mat4 realCamWorld = glm::inverse(camera.GetViewMatrix());
                            glm::mat4 mIn  = transform.transform.matrix;
                            glm::mat4 mOut = targetTransform.transform.matrix;
                            auto clean = [](glm::mat4 m) {
                                m[0] = glm::vec4(glm::normalize(glm::vec3(m[0])), 0);
                                m[1] = glm::vec4(glm::normalize(glm::vec3(m[1])), 0);
                                m[2] = glm::vec4(glm::normalize(glm::vec3(m[2])), 0);
                                return m;
                            };
                            static const glm::mat4 rot180 = glm::rotate(glm::mat4(1.f), glm::pi<float>(), {0, 1, 0});
                            glm::mat4 portalTransition = clean(mOut) * rot180 * glm::inverse(clean(mIn));
                            glm::mat4 virtualCamWorld  = portalTransition * realCamWorld;
                        
                            GlobalUbo portalUbo   = ubo;
                            portalUbo.view        = glm::inverse(virtualCamWorld);
                            portalUbo.invViewProj = glm::inverse(portalUbo.projection * portalUbo.view);
                            portalUbo.camPos      = glm::vec3(virtualCamWorld[3]);
                        
                            glm::vec3 exitPortalPos    = glm::vec3(mOut[3]);
                            glm::vec3 exitPortalNormal = -glm::normalize(glm::vec3(mOut[2]));
                            // Знак минус: local +Z портала смотрит наружу (от сцены),
                            // нам нужна нормаль внутрь. Если клипается не та сторона — убери минус.

                            portalUbo.projection = computeObliqueProjection(
                                portalUbo.projection, portalUbo.view,
                                exitPortalPos, exitPortalNormal);
                            
                            // Обязательно пересчитать после модификации projection!
                            portalUbo.invViewProj = glm::inverse(portalUbo.projection * portalUbo.view);
                            
                            portalUboBuffers[portalCounter][frameIndex]->writeToBuffer(&portalUbo);
                            portalUboBuffers[portalCounter][frameIndex]->flush();
                        
                            FrameInfo portalFrameInfo  = frameInfo;
                            portalFrameInfo.globalDescriptorSet = portalDescriptorSets[portalCounter][frameIndex];
                        
                            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, portalCounter + 1);
                            simpleRenderSystem->renderEntities(portalFrameInfo, registry, storageSet, textureSet,
                                                               *cullingSystem, totalSubMeshCount, true);
                            portalCounter++;
                        }
                    }

                    vkCmdEndRenderPass(cmd); });

                renderPipeline.addPass("Hi-Z Pass", {RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)}, [&](VkCommandBuffer cmd)
                                       { hizSystem->compute(cmd, extent); });
                renderPipeline.addPass("GTAO Pass", {RenderPipeline::createImageBarrier(gtaoOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       { gtaoSystem->compute(cmd, globalDescriptorSets[frameIndex], gtaoSet, extent.width, extent.height); });
                renderPipeline.addPass("Compute Lighting", {RenderPipeline::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    std::vector<VkDescriptorSet> computeSets = { globalDescriptorSets[frameIndex], gBufferSet, shadowSet, lightSet, computeOutputSet, rcSystem->getIrradianceSet(), gtaoSet };
                    lightingSystem->computeLighting(cmd, computeSets, extent.width, extent.height); });
                renderPipeline.addPass("Radiance Cascades GI", {RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)},
                                       [&](VkCommandBuffer cmd)
                                       {
                    glm::vec3 sceneMin(-32.0f, -32.0f, -32.0f);
                    glm::vec3 sceneMax( 32.0f,  32.0f,  32.0f);
                    rcSystem->dispatch(cmd, globalDescriptorSets[frameIndex], gBufferSet, ubo.invViewProj, camera.Position, sceneMin, sceneMax, extent, rtSet, storageSet, textureSet); });
                VkImage swapChainImage = lveRenderer.getCurrentSwapChainImage();
                renderPipeline.addPass("Blit and UI", {RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    VkImageMemoryBarrier swapToDst = RenderPipeline::createImageBarrier(swapChainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapToDst);
                    VkImageBlit blit{};
                    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.dstOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    vkCmdBlitImage(cmd, hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                    VkImageMemoryBarrier swapToAttach = RenderPipeline::createImageBarrier(swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapToAttach);
                    lveRenderer.beginSwapChainRenderPass(cmd);
                    ui.Draw(lveWindow, camera, registry, cmd);
                    lveRenderer.endSwapChainRenderPass(cmd); });
                renderPipeline.addPass("Reset Layouts", {RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [](VkCommandBuffer cmd) {});
                renderPipeline.execute(commandBuffer);
                lveRenderer.endFrame();
            }
        }
        vkDeviceWaitIdle(lveDevice.device());
    }
    void FirstApp::initCompute(VkDescriptorSetLayout globalSetLayouts)
    {
        VkExtent3D extent = {lveWindow.getExtent().width, lveWindow.getExtent().height, 1};
        hdrOutputTexture = std::make_unique<BurnhopeTexture>(
            lveDevice,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);
        hdrOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        gtaoOutputTexture = std::make_unique<BurnhopeTexture>(
            lveDevice,
            VK_FORMAT_R8_UNORM,
            extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_SAMPLE_COUNT_1_BIT);
        gtaoOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        gBufferLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .build();
        outputLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();
        shadowLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                              .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                              .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();
        lightLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                             .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                             .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                             .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                             .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                             .build();
        irradianceLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                  .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                  .build();
        gtaoLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                            .build();
        rtLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                          .addBinding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_COMPUTE_BIT)
                          .build();
        faceMatricesBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(PointFaceMatrices),
            100,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        faceMatricesBuffer->map();
        auto normInfo = gBuffer->getNormalRoughness()->getImageInfo();
        auto albInfo = gBuffer->getAlbedoMetallic()->getImageInfo();
        auto extraInfo = gBuffer->getHeightAO()->getImageInfo();
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gBuffer->getDepth()->getSampler();
        depthInfo.imageView = gBuffer->getDepth()->getImageView();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
            .writeImage(0, &normInfo)
            .writeImage(1, &albInfo)
            .writeImage(2, &extraInfo)
            .writeImage(3, &depthInfo)
            .build(gBufferSet);
        VkDescriptorImageInfo outImgInfo{};
        outImgInfo.imageView = hdrOutputTexture->getImageView();
        outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*outputLayoutPtr, *globalPool)
            .writeImage(0, &outImgInfo)
            .build(computeOutputSet);
        lightUboBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(LightUBOData), 1,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        lightUboBuffer->map();
        VkImageViewCreateInfo arrayViewInfo{};
        arrayViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        arrayViewInfo.image = shadowSystem->getCSM()->getTexture()->getImage();
        arrayViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        arrayViewInfo.format = shadowSystem->getCSM()->getTexture()->getFormat();
        arrayViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        arrayViewInfo.subresourceRange.baseMipLevel = 0;
        arrayViewInfo.subresourceRange.levelCount = 1;
        arrayViewInfo.subresourceRange.baseArrayLayer = 0;
        arrayViewInfo.subresourceRange.layerCount = BurnhopeCSM::CASCADE_COUNT;
        VkImageView csmArrayView;
        if (vkCreateImageView(lveDevice.device(), &arrayViewInfo, nullptr, &csmArrayView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create 2D Array View for CSM!");
        }
        VkDescriptorImageInfo csmInfo{};
        csmInfo.sampler = shadowSystem->getCSM()->getTexture()->getSampler();
        csmInfo.imageView = csmArrayView;
        csmInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto atlasInfo = shadowSystem->getAtlas()->getTexture()->getImageInfo();
        auto noiseInfo = defaultWhiteTex->getImageInfo();
        BurnhopeDescriptorWriter(*shadowLayoutPtr, *globalPool)
            .writeImage(0, &csmInfo)
            .writeImage(1, &atlasInfo)
            .writeImage(2, &noiseInfo)
            .build(shadowSet);
        struct LightGrid
        {
            uint32_t offset;
            uint32_t count;
        };
        dummyGridBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(LightGrid),
            16 * 9 * 24,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        dummyGridBuffer->map();
        memset(dummyGridBuffer->getMappedMemory(), 0, sizeof(LightGrid) * 16 * 9 * 24);
        dummyIndexBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(uint32_t),
            1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        auto gridInfo = dummyGridBuffer->descriptorInfo();
        auto indexInfo = dummyIndexBuffer->descriptorInfo();
        auto lightBufInfo = lightUboBuffer->descriptorInfo();
        auto faceMatInfo = faceMatricesBuffer->descriptorInfo();
        BurnhopeDescriptorWriter(*lightLayoutPtr, *globalPool)
            .writeBuffer(0, &lightBufInfo)
            .writeBuffer(1, &gridInfo)
            .writeBuffer(2, &indexInfo)
            .writeBuffer(3, &faceMatInfo)
            .build(lightSet);
        std::vector<VkDescriptorSetLayout> computeLayouts = {
            globalSetLayouts,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            shadowLayoutPtr->getDescriptorSetLayout(),
            lightLayoutPtr->getDescriptorSetLayout(),
            outputLayoutPtr->getDescriptorSetLayout(),
            irradianceLayoutPtr->getDescriptorSetLayout(),
            gtaoLayoutPtr->getDescriptorSetLayout(), rtLayoutPtr->getDescriptorSetLayout()};
        lightingSystem = std::make_unique<DeferredLightingSystem>(lveDevice, computeLayouts);
        rcSystem = std::make_unique<RadianceCascadesSystem>(
            lveDevice,
            lveWindow.getExtent(),
            *globalPool,
            globalSetLayout->getDescriptorSetLayout(),
            gBufferLayoutPtr->getDescriptorSetLayout(),
            hdrOutputTexture->getImageView(),
            hdrOutputTexture->getSampler(),
            rtLayoutPtr->getDescriptorSetLayout(),
            simpleRenderSystem->getRenderSystemLayout()->getDescriptorSetLayout(),
            simpleRenderSystem->getTextureLayout()->getDescriptorSetLayout());
        auto normalInfo = gBuffer->getNormalRoughness()->getImageInfo();
        VkDescriptorImageInfo gtaoOutInfo{};
        gtaoOutInfo.imageView = gtaoOutputTexture->getImageView();
        gtaoOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*gtaoLayoutPtr, *globalPool)
            .writeImage(0, &depthInfo)
            .writeImage(1, &normalInfo)
            .writeImage(2, &gtaoOutInfo)
            .build(gtaoSet);
        std::vector<VkDescriptorSetLayout> gtaoLayouts = {
            globalSetLayouts,
            gtaoLayoutPtr->getDescriptorSetLayout()};

        globalPool->allocateDescriptor(rtLayoutPtr->getDescriptorSetLayout(), rtSet);

        gtaoSystem = std::make_unique<GTAOSystem>(lveDevice, gtaoLayouts);
        hizSystem = std::make_unique<HiZSystem>(
            lveDevice, lveWindow.getExtent(), *globalPool,
            gBuffer->getDepth()->getImageView(), gBuffer->getDepth()->getSampler());
    }
    void FirstApp::rebuildGBufferDescriptorSets()
    {
        if (gBufferSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {gBufferSet};
            globalPool->freeDescriptors(toFree);
            gBufferSet = VK_NULL_HANDLE;
        }
        if (computeOutputSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {computeOutputSet};
            globalPool->freeDescriptors(toFree);
            computeOutputSet = VK_NULL_HANDLE;
        }
        auto normInfo = gBuffer->getNormalRoughness()->getImageInfo();
        auto albInfo = gBuffer->getAlbedoMetallic()->getImageInfo();
        auto extraInfo = gBuffer->getHeightAO()->getImageInfo();
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gBuffer->getDepth()->getSampler();
        depthInfo.imageView = gBuffer->getDepth()->getImageView();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bool ok = BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
                      .writeImage(0, &normInfo)
                      .writeImage(1, &albInfo)
                      .writeImage(2, &extraInfo)
                      .writeImage(3, &depthInfo)
                      .build(gBufferSet);
        if (!ok || gBufferSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to rebuild gBufferSet!");
        }
        hdrOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        if (gtaoSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {gtaoSet};
            globalPool->freeDescriptors(toFree);
            gtaoSet = VK_NULL_HANDLE;
        }
        VkExtent3D extent = {lveWindow.getExtent().width, lveWindow.getExtent().height, 1};
        gtaoOutputTexture = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R8_UNORM, extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
        gtaoOutputTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        auto normalInfo = gBuffer->getNormalRoughness()->getImageInfo();
        VkDescriptorImageInfo gtaoOutInfo{};
        gtaoOutInfo.imageView = gtaoOutputTexture->getImageView();
        gtaoOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        hizSystem = std::make_unique<HiZSystem>(
            lveDevice, lveWindow.getExtent(), *globalPool,
            gBuffer->getDepth()->getImageView(), gBuffer->getDepth()->getSampler());
        if (hizSystem && cullingSystem)
        {
            cullingSystem->updateHiZDescriptor(hizSystem->getHiZImageInfo());
        }
        BurnhopeDescriptorWriter(*gtaoLayoutPtr, *globalPool)
            .writeImage(0, &depthInfo)
            .writeImage(1, &normalInfo)
            .writeImage(2, &gtaoOutInfo)
            .build(gtaoSet);
        VkDescriptorImageInfo outImgInfo{};
        outImgInfo.imageView = hdrOutputTexture->getImageView();
        outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        ok = BurnhopeDescriptorWriter(*outputLayoutPtr, *globalPool)
                 .writeImage(0, &outImgInfo)
                 .build(computeOutputSet);
        if (!ok || computeOutputSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to rebuild computeOutputSet!");
        }
    }
    void FirstApp::buildTLAS(entt::registry &registry)
    {
        auto pfnCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkCreateAccelerationStructureKHR");
        auto pfnGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkGetAccelerationStructureBuildSizesKHR");
        auto pfnCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkCmdBuildAccelerationStructuresKHR");
        auto pfnGetBufferDeviceAddress = (PFN_vkGetBufferDeviceAddress)vkGetDeviceProcAddr(lveDevice.device(), "vkGetBufferDeviceAddress");

        if (!pfnCreateAccelerationStructureKHR)
        {
            throw std::runtime_error("[FATAL] Ошибка загрузки функций для TLAS");
        }

        std::vector<VkAccelerationStructureInstanceKHR> instances;

        auto view = registry.view<TransformComponent, MeshComponent>();
        uint32_t customIndex = 0;

        for (auto [entity, transformComp, meshComp] : view.each())
        {
            if (!meshComp.model || !meshComp.isVisible)
                continue;

            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = toVkMatrix(transformComp.transform.matrix);
            instance.instanceCustomIndex = customIndex++;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = meshComp.model->getBLASAddress();

            instances.push_back(instance);
        }

        if (instances.empty())
            return;

        VkDeviceSize instancesBufferSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        instancesBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(VkAccelerationStructureInstanceKHR),
            instances.size(),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        BurnhopeBuffer stagingBuffer{
            lveDevice, sizeof(VkAccelerationStructureInstanceKHR), static_cast<uint32_t>(instances.size()),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        stagingBuffer.map();
        stagingBuffer.writeToBuffer((void *)instances.data());
        lveDevice.copyBuffer(stagingBuffer.getBuffer(), instancesBuffer->getBuffer(), instancesBufferSize);

        VkBufferDeviceAddressInfo instanceAddressInfo{};
        instanceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        instanceAddressInfo.buffer = instancesBuffer->getBuffer();
        VkDeviceAddress instanceAddress = pfnGetBufferDeviceAddress(lveDevice.device(), &instanceAddressInfo);

        VkAccelerationStructureGeometryKHR tlasGeometry{};
        tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
        tlasGeometry.geometry.instances.data.deviceAddress = instanceAddress;

        VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildInfo.geometryCount = 1;
        buildInfo.pGeometries = &tlasGeometry;

        uint32_t primitiveCount = static_cast<uint32_t>(instances.size());

        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        pfnGetAccelerationStructureBuildSizesKHR(lveDevice.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        tlasBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeInfo.accelerationStructureSize, 1,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = tlasBuffer->getBuffer();
        createInfo.size = sizeInfo.accelerationStructureSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        pfnCreateAccelerationStructureKHR(lveDevice.device(), &createInfo, nullptr, &tlasHandle);

        BurnhopeBuffer scratchBuffer(
            lveDevice, sizeInfo.buildScratchSize, 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkBufferDeviceAddressInfo scratchAddressInfo{};
        scratchAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        scratchAddressInfo.buffer = scratchBuffer.getBuffer();
        buildInfo.scratchData.deviceAddress = pfnGetBufferDeviceAddress(lveDevice.device(), &scratchAddressInfo);
        buildInfo.dstAccelerationStructure = tlasHandle;

        VkAccelerationStructureBuildRangeInfoKHR buildRange{};
        buildRange.primitiveCount = primitiveCount;
        buildRange.primitiveOffset = 0;
        buildRange.firstVertex = 0;
        buildRange.transformOffset = 0;
        const VkAccelerationStructureBuildRangeInfoKHR *pBuildRange = &buildRange;

        VkCommandBuffer commandBuffer = lveDevice.beginSingleTimeCommands();
        pfnCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, &pBuildRange);
        lveDevice.endSingleTimeCommands(commandBuffer);

        std::cout << "[RT] Successfully built TLAS with " << instances.size() << " instances!" << std::endl;
    }
    void FirstApp::loadGameObjects(entt::registry &registry)
    {
        std::shared_ptr<BurnhopeModel> lveModel =
            BurnhopeModel::createModelFromFile(lveDevice, "models/PortalsPlaceholder1.gltf");

        auto modelEntity = registry.create();
        registry.emplace<TagComponent>(modelEntity, "Room1");
        registry.emplace<IDComponent>(modelEntity);
        registry.emplace<HierarchyComponent>(modelEntity);
        auto &transform = registry.emplace<TransformComponent>(modelEntity);
        transform.transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
        transform.transform.rotation = glm::vec3(0.0f, 0.0f, 0.0f);
        transform.transform.scale = glm::vec3(0.25f, 0.25f, 0.25f);
        // glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.transform.position);
        // transform.transform.matrix = glm::scale(translation, transform.transform.scale);
        transform.transform.updateMatrixIfNeeded();
        auto &mesh = registry.emplace<MeshComponent>(modelEntity);
        mesh.model = lveModel;
        mesh.materials = lveModel->materials;

        std::shared_ptr<BurnhopeModel> lveModel2 =
            BurnhopeModel::createModelFromFile(lveDevice, "models/PortalsPlaceholder2.gltf");

        auto modelEntity2 = registry.create();
        registry.emplace<TagComponent>(modelEntity2, "Room2");
        registry.emplace<IDComponent>(modelEntity2);
        registry.emplace<HierarchyComponent>(modelEntity2);
        auto &transform2 = registry.emplace<TransformComponent>(modelEntity2);
        transform2.transform.position = glm::vec3(-15.0f, 0.0f, 0.0f);
        transform2.transform.rotation = glm::vec3(0.0f, -90.0f, 0.0f);
        transform2.transform.scale = glm::vec3(0.25f, 0.25f, 0.25f);
        // glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.transform.position);
        // transform.transform.matrix = glm::scale(translation, transform.transform.scale);
        transform2.transform.updateMatrixIfNeeded();
        auto &mesh2 = registry.emplace<MeshComponent>(modelEntity2);
        mesh2.model = lveModel2;
        mesh2.materials = lveModel2->materials;

        auto portalA = registry.create();
        registry.emplace<TagComponent>(portalA, "Portal_A");
        registry.emplace<IDComponent>(portalA);
        auto &transA = registry.emplace<TransformComponent>(portalA);
        transA.transform.position = {0.0f, 2.1f, -3.75f};
        transA.transform.rotation = {0.0f, 0.0f, 0.0f};
        transA.transform.scale = {2.0f, 2.0f, 2.0f};
        transA.transform.updateMatrixIfNeeded();

        registry.emplace<PortalComponent>(portalA);

        auto portalB = registry.create();
        registry.emplace<TagComponent>(portalB, "Portal_B");
        registry.emplace<IDComponent>(portalB);
        auto &transB = registry.emplace<TransformComponent>(portalB);
        transB.transform.position = {-11.25f, 2.1f, 0.0f};
        transB.transform.rotation = {0.0f, -90.0f, 0.0f};
        transB.transform.scale = {2.0f, 2.0f, 2.0f};
        transB.transform.updateMatrixIfNeeded();

        registry.emplace<PortalComponent>(portalB);

        auto portalC = registry.create();
        registry.emplace<TagComponent>(portalC, "Portal_C");
        registry.emplace<IDComponent>(portalC);
        auto &transC = registry.emplace<TransformComponent>(portalC);
        transC.transform.position = {-3.5f, 2.65f, -3.42f};
        transC.transform.rotation = {0.0f, 0.0f, 0.0f};
        transC.transform.scale = {1.0f, 1.0f, 1.0f};
        transC.transform.updateMatrixIfNeeded();

        registry.emplace<PortalComponent>(portalC);

        auto portalD = registry.create();
        registry.emplace<TagComponent>(portalD, "Portal_D");
        registry.emplace<IDComponent>(portalD);
        auto &transD = registry.emplace<TransformComponent>(portalD);
        transD.transform.position = {-15.2f, 2.250f, 4.95f};
        transD.transform.rotation = {0.0f, 180.0f, 0.0f};
        transD.transform.scale = {1.0f, 1.0f, 1.0f};
        transD.transform.updateMatrixIfNeeded();

        registry.emplace<PortalComponent>(portalD);

        registry.get<PortalComponent>(portalA).targetPortal = portalB;
        registry.get<PortalComponent>(portalB).targetPortal = portalA;

        registry.get<PortalComponent>(portalC).targetPortal = portalD;
        registry.get<PortalComponent>(portalD).targetPortal = portalC;

        /*
                std::shared_ptr<BurnhopeModel> lveModel3 =
                    BurnhopeModel::createModelFromFile(lveDevice, "models/PortalsPlaceholder3.gltf");

                auto modelEntity3 = registry.create();
                registry.emplace<TagComponent>(modelEntity3, "Cube");
                registry.emplace<IDComponent>(modelEntity3);
                registry.emplace<HierarchyComponent>(modelEntity3);
                auto &transform3 = registry.emplace<TransformComponent>(modelEntity3);
                transform3.transform.position = glm::vec3(-73.0f, 1.0f, 5.0f);
                transform3.transform.rotation = glm::vec3(0.0f, 0.0f, 15.0f);
                transform3.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
                //glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.transform.position);
                //transform.transform.matrix = glm::scale(translation, transform.transform.scale);
                transform3.transform.updateMatrixIfNeeded();
                auto &mesh3 = registry.emplace<MeshComponent>(modelEntity3);
                mesh3.model = lveModel3;
                mesh3.materials = lveModel3->materials;



                std::shared_ptr<BurnhopeModel> lveModel4 =
                    BurnhopeModel::createModelFromFile(lveDevice, "models/PortalsPlaceholder4.gltf");

                auto modelEntity4 = registry.create();
                registry.emplace<TagComponent>(modelEntity4, "Cube2");
                registry.emplace<IDComponent>(modelEntity4);
                registry.emplace<HierarchyComponent>(modelEntity4);
                auto &transform4 = registry.emplace<TransformComponent>(modelEntity4);
                transform4.transform.position = glm::vec3(-68.0f, -5.0f, 3.0f);
                transform4.transform.rotation = glm::vec3(0.0f, 0.0f, -25.0f);
                transform4.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
                //glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.transform.position);
                //transform.transform.matrix = glm::scale(translation, transform.transform.scale);
                transform4.transform.updateMatrixIfNeeded();
                auto &mesh4 = registry.emplace<MeshComponent>(modelEntity4);
                mesh4.model = lveModel4;
                mesh4.materials = lveModel4->materials;

        */
        auto sunEntity = registry.create();
        registry.emplace<TagComponent>(sunEntity, "Sun");
        registry.emplace<IDComponent>(sunEntity);
        registry.emplace<HierarchyComponent>(sunEntity);
        auto &sunTransform = registry.emplace<TransformComponent>(sunEntity);
        sunTransform.transform.rotation = glm::vec3(-45.0f, 0.0f, -35.0f);

        auto &sunLight = registry.emplace<LightComponent>(sunEntity);
        sunLight.light.enable = true;
        sunLight.light.type = LightType::Directional;
        sunLight.light.color = glm::vec3(1.0f, 1.0f, 1.0f);
        sunLight.light.intensity = 1.0f;
        sunLight.light.radius = 500.0f;
        sunLight.light.castShadows = true;
        sunLight.light.mobility = LightMobility::Movable;
    }

}