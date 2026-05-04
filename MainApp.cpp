#include "MainApp.hpp"
#include "Render/Camera.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <chrono>
#include <stdexcept>
#include <array>
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
                         .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2000)
                         .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50)
                         .build();
        
        
        defaultWhiteTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/white.png");
        defaultNormalTex = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/white.png");
        shadowSystem = std::make_unique<BurnhopeShadowSystem>(lveDevice);

        defaultWhiteMaterial = std::make_shared<Material>();
        defaultWhiteMaterial->setAlbedo(defaultWhiteTex);
        defaultWhiteMaterial->setNormal(defaultNormalTex);
        gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, lveWindow.getExtent());
        
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
        
    }
    glm::mat4 shadowPerspective(float fovY, float aspect, float zNear, float zFar)
    {
        glm::mat4 proj = glm::perspective(glm::radians(fovY), aspect, zNear, zFar);
        proj[1][1] *= -1.0f;
        return proj;
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
            uboBuffers[i] = std::make_unique<BurnhopeBuffer>(
                lveDevice, sizeof(GlobalUbo), 1,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
            uboBuffers[i]->map();
        }

        auto globalSetLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                   .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT)
                                   .build();

        std::vector<VkDescriptorSet> globalDescriptorSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        for (int i = 0; i < globalDescriptorSets.size(); i++)
        {
            auto bufferInfo = uboBuffers[i]->descriptorInfo();
            BurnhopeDescriptorWriter(*globalSetLayout, *globalPool)
                .writeBuffer(0, &bufferInfo)
                .build(globalDescriptorSets[i]);
        }
        initCompute(globalSetLayout->getDescriptorSetLayout());
        

        GeometryRenderSystem simpleRenderSystem{
            lveDevice,
            gBuffer->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout()};

        
        RebuildBatches(registry, simpleRenderSystem);

        shadowObjectLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                    .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                                VK_SHADER_STAGE_VERTEX_BIT)
                                    .build();

        
        shadowRenderSystem = std::make_unique<ShadowRenderSystem>(
            lveDevice,
            shadowSystem->getCSM()->getRenderPass(), 
            shadowObjectLayoutPtr->getDescriptorSetLayout());

        
        
        if (objectBuffer)
        {
            auto objInfo = objectBuffer->descriptorInfo();
            BurnhopeDescriptorWriter(*shadowObjectLayoutPtr, *globalPool)
                .writeBuffer(0, &objInfo)
                .build(shadowObjectSet);
        }

        Camera camera(WIDTH, HEIGHT, glm::vec3(0.0f, 0.0f, 0.0f));

        auto currentTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        auto fpsTimer = currentTime;
        VkExtent2D lastExtent = lveWindow.getExtent();
        while (!lveWindow.shouldClose())
        {
            glfwPollEvents();
            
            
            auto extent = lveWindow.getExtent();
            while (extent.width == 0 || extent.height == 0)
            {
                extent = lveWindow.getExtent();
                glfwWaitEvents();
            }
            camera.width = lveWindow.getExtent().width;
            camera.height = lveWindow.getExtent().height;
            
            VkExtent2D swapExtent = lveRenderer.getSwapChainExtent();
            if (extent.width != swapExtent.width || extent.height != swapExtent.height)
            {
                vkDeviceWaitIdle(lveDevice.device());

                
                lveRenderer.recreateSwapChain(); 

                VkExtent2D newExtent = lveRenderer.getSwapChainExtent();

                gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, newExtent);

                hdrOutputTexture = std::make_unique<BurnhopeTexture>(
                    lveDevice,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VkExtent3D{newExtent.width, newExtent.height, 1},
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_STORAGE_BIT |
                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT,
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
            currentTime = newTime;

            frameCount++;
            if (std::chrono::duration<float>(newTime - fpsTimer).count() >= 1.0f)
            {
                std::cout << "FPS: " << frameCount << std::endl;
                frameCount = 0;
                fpsTimer = newTime;
            }

            camera.Inputs(lveWindow.getGLFWwindow(), frameTime);

            if (auto commandBuffer = lveRenderer.beginFrame())
            {
                int frameIndex = lveRenderer.getFrameIndex();

                FrameInfo frameInfo{
                    frameIndex, frameTime, commandBuffer,
                    camera, globalDescriptorSets[frameIndex], *globalPool};

                
                
                
                GlobalUbo ubo{};
                shadowSystem->updateLights(registry, camera.Position);
                {
                    const auto &uboData = shadowSystem->getLightUBO();
                    lightUboBuffer->writeToBuffer((void *)&uboData);
                    lightUboBuffer->flush();
                }
                
                faceMatricesBuffer->writeToBuffer(const_cast<PointFaceMatrices *>(shadowSystem->getFaceMatricesData()));
                faceMatricesBuffer->flush();
                ubo.projection = camera.GetProjectionMatrix(45.0f, 0.01f, 1000.0f);
                ubo.view = camera.GetViewMatrix();
                ubo.invViewProj = glm::inverse(ubo.projection * ubo.view);
                ubo.camPos = camera.Position;
                ubo.zNear = 0.1f;
                ubo.zFar = 1000.0f;
                ubo.screenSize = glm::vec4(lveWindow.getExtent().width, lveWindow.getExtent().height, 0.f, 0.f);
                ubo.sunDir = shadowSystem->getSunDir();
                ubo.lightSize = 1.0f;

                auto cascadeMats = shadowSystem->getCSM()->calculateMatrices(
                    camera, shadowSystem->getSunDir(),
                    {shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1],
                     shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]});

                for (int i = 0; i < 4; i++)
                    ubo.sunLightSpaceMatrices[i] = cascadeMats[i];
                ubo.cascadeSplits = glm::vec4(
                    shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1],
                    shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]);

                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                
                RenderGraph renderGraph;

                
                
                
                std::vector<VkImageMemoryBarrier> shadowBarriersBegin(2);
                shadowBarriersBegin[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                shadowBarriersBegin[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                shadowBarriersBegin[0].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                shadowBarriersBegin[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shadowBarriersBegin[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shadowBarriersBegin[0].image = shadowSystem->getCSM()->getTexture()->getImage();
                shadowBarriersBegin[0].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, BurnhopeCSM::CASCADE_COUNT};
                shadowBarriersBegin[0].srcAccessMask = 0;
                shadowBarriersBegin[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                shadowBarriersBegin[1] = shadowBarriersBegin[0];
                shadowBarriersBegin[1].image = shadowSystem->getAtlas()->getTexture()->getImage();
                shadowBarriersBegin[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

                renderGraph.addPass("Shadow Maps Pass", shadowBarriersBegin, [&](VkCommandBuffer cmd)
                                    {
        
        for (int i = 0; i < BurnhopeCSM::CASCADE_COUNT; i++) {
            VkRenderPassBeginInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpInfo.renderPass = shadowSystem->getCSM()->getRenderPass();
            rpInfo.framebuffer = shadowSystem->getCSM()->getFramebuffer(i);
            rpInfo.renderArea.offset = { 0, 0 };
            rpInfo.renderArea.extent = { BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE };
            VkClearValue clearVal{}; clearVal.depthStencil = { 1.0f, 0 };
            rpInfo.clearValueCount = 1; rpInfo.pClearValues = &clearVal;

            vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
            VkViewport vp{}; vp.width = vp.height = (float)BurnhopeCSM::SHADOW_MAP_SIZE;
            vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &vp);
            VkRect2D sc{ {0,0}, {BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE} };
            vkCmdSetScissor(cmd, 0, 1, &sc);

            shadowRenderSystem->renderShadow(cmd, cascadeMats[i], *cullingSystem, registry, shadowObjectSet);
            vkCmdEndRenderPass(cmd);
        }

        
        VkRenderPassBeginInfo clearPass{};
        clearPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        clearPass.renderPass = shadowSystem->getAtlas()->getRenderPass();
        clearPass.framebuffer = shadowSystem->getAtlas()->getFramebuffer();
        clearPass.renderArea.offset = { 0, 0 };
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
                auto vp = ubo.projection * ubo.view;
                auto planes = CullingSystem::extractFrustumPlanes(vp);

                
                cullingSystem->dispatchCulling(commandBuffer, vp, ubo.camPos, planes, totalSubMeshCount);

                static bool debugDone = false;
                if (!debugDone)
                {
                    vkDeviceWaitIdle(lveDevice.device());

                    
                    BurnhopeBuffer readback(lveDevice,
                                            sizeof(VkDrawIndexedIndirectCommand), totalSubMeshCount,
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

                    
                    auto cmd = lveDevice.beginSingleTimeCommands();
                    VkBufferCopy copy{};
                    copy.size = sizeof(VkDrawIndexedIndirectCommand) * totalSubMeshCount;
                    vkCmdCopyBuffer(cmd, cullingSystem->getDrawCommandBuffer(),
                                    readback.getBuffer(), 1, &copy);
                    lveDevice.endSingleTimeCommands(cmd);

                    readback.map();
                    auto *cmds = reinterpret_cast<VkDrawIndexedIndirectCommand *>(readback.getMappedMemory());

                    uint32_t visible = 0, culled = 0;
                    for (uint32_t i = 0; i < totalSubMeshCount; i++)
                    {
                        if (cmds[i].instanceCount > 0)
                            visible++;
                        else
                            culled++;
                    }
                    std::cout << "Culling result: " << visible << " visible, "
                              << culled << " culled out of " << totalSubMeshCount << "\n";
                    debugDone = true;
                }
                
                
                
                std::vector<VkImageMemoryBarrier> shadowBarriersEnd(2);
                shadowBarriersEnd[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                shadowBarriersEnd[0].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                shadowBarriersEnd[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                shadowBarriersEnd[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shadowBarriersEnd[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                shadowBarriersEnd[0].image = shadowSystem->getCSM()->getTexture()->getImage();
                shadowBarriersEnd[0].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, BurnhopeCSM::CASCADE_COUNT};
                shadowBarriersEnd[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                shadowBarriersEnd[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                shadowBarriersEnd[1] = shadowBarriersEnd[0];
                shadowBarriersEnd[1].image = shadowSystem->getAtlas()->getTexture()->getImage();
                shadowBarriersEnd[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

                renderGraph.addPass("G-Buffer Pass", shadowBarriersEnd, [&](VkCommandBuffer cmd)
                                    {
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = gBuffer->getRenderPass();
        renderPassInfo.framebuffer = gBuffer->getFramebuffer();
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = lveWindow.getExtent();

        std::array<VkClearValue, 4> clearValues{};
        clearValues[3].depthStencil = { 1.0f, 0 };
        renderPassInfo.clearValueCount = (uint32_t)clearValues.size();
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.width = (float)lveWindow.getExtent().width;
        viewport.height = (float)lveWindow.getExtent().height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{ {0, 0}, lveWindow.getExtent() };
        vkCmdSetScissor(cmd, 0, 1, &scissor);



        
        simpleRenderSystem.renderEntities(frameInfo, registry, 
                                        storageSet, textureSet,
                                        *cullingSystem, totalSubMeshCount);
        vkCmdEndRenderPass(cmd); });

                std::vector<VkImageMemoryBarrier> hizBarriers(1);
                
                hizBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                hizBarriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                hizBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                hizBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                hizBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                hizBarriers[0].image = gBuffer->getDepth()->getImage();
                hizBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                hizBarriers[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                hizBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                renderGraph.addPass("Hi-Z Pass", hizBarriers, [&](VkCommandBuffer cmd)
                                    { hizSystem->compute(cmd, lveWindow.getExtent()); });
                
                
                
                std::vector<VkImageMemoryBarrier> gtaoBarriers(1);
                gtaoBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                gtaoBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                gtaoBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                gtaoBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                gtaoBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                gtaoBarriers[0].image = gtaoOutputTexture->getImage();
                gtaoBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                gtaoBarriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                gtaoBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

                renderGraph.addPass("GTAO Pass", gtaoBarriers, [&](VkCommandBuffer cmd)
                                    {
        
        gtaoSystem->compute(cmd, globalDescriptorSets[frameIndex], gtaoSet, 
                            lveWindow.getExtent().width, lveWindow.getExtent().height); });
                
                
                
                std::vector<VkImageMemoryBarrier> gBufBarriers(4);
                VkImage gBufImages[4] = {
                    gBuffer->getNormalRoughness()->getImage(),
                    gBuffer->getAlbedoMetallic()->getImage(),
                    gBuffer->getHeightAO()->getImage(),
                    gBuffer->getDepth()->getImage()};
                VkImageAspectFlags aspects[4] = {VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_ASPECT_DEPTH_BIT};

                for (int i = 0; i < 4; i++)
                {
                    gBufBarriers[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    gBufBarriers[i].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    gBufBarriers[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    gBufBarriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    gBufBarriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    gBufBarriers[i].image = gBufImages[i];
                    gBufBarriers[i].subresourceRange = {aspects[i], 0, 1, 0, 1};
                    gBufBarriers[i].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    gBufBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                }
                gBufBarriers[3].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                renderGraph.addPass("Compute Lighting", gBufBarriers, [&](VkCommandBuffer cmd)
                                    {
        std::vector<VkDescriptorSet> computeSets = {
            globalDescriptorSets[frameIndex], gBufferSet, shadowSet, lightSet, computeOutputSet,rcSystem->getIrradianceSet() ,
        gtaoSet 
        };
        lightingSystem->computeLighting(cmd, computeSets, lveWindow.getExtent().width, lveWindow.getExtent().height); });

                
                
                
                std::vector<VkImageMemoryBarrier> rcBarriers(1);
                rcBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                rcBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                rcBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                rcBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                rcBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                rcBarriers[0].image = hdrOutputTexture->getImage();
                rcBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                rcBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                rcBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                renderGraph.addPass("Radiance Cascades GI", rcBarriers, [&](VkCommandBuffer cmd)
                                    {
        glm::vec3 sceneMin(-20.0f, -2.0f, -20.0f);
        glm::vec3 sceneMax( 20.0f, 10.0f,  20.0f);

        rcSystem->dispatch(
            cmd,
            globalDescriptorSets[frameIndex],
            gBufferSet,
            ubo.invViewProj,
            camera.Position,
            sceneMin,
            sceneMax,
            lveWindow.getExtent()
        ); });
                
                
                
                std::vector<VkImageMemoryBarrier> blitBarriers(2);
                
                blitBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                blitBarriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                blitBarriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blitBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                blitBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                blitBarriers[0].image = hdrOutputTexture->getImage();
                blitBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                blitBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                blitBarriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

                
                blitBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                blitBarriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                blitBarriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                blitBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                blitBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                blitBarriers[1].image = gBuffer->getDepth()->getImage();
                blitBarriers[1].subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                blitBarriers[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                blitBarriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

                renderGraph.addPass("Blit and UI", blitBarriers, [&](VkCommandBuffer cmd)
                                    {
        VkImage swapChainImage = lveRenderer.getCurrentSwapChainImage();

        
        VkImageMemoryBarrier swapToDst{};
        swapToDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapToDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        swapToDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapToDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToDst.image = swapChainImage;
        swapToDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        swapToDst.srcAccessMask = 0;
        swapToDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 
                             0, 0, nullptr, 0, nullptr, 1, &swapToDst);

        
        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[1] = { (int32_t)lveWindow.getExtent().width, (int32_t)lveWindow.getExtent().height, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.dstOffsets[1] = { (int32_t)lveWindow.getExtent().width, (int32_t)lveWindow.getExtent().height, 1 };

        vkCmdBlitImage(cmd, hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        
        VkImageMemoryBarrier swapToAttach{};
        swapToAttach.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        swapToAttach.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        swapToAttach.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        swapToAttach.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToAttach.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        swapToAttach.image = swapChainImage;
        swapToAttach.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        swapToAttach.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        swapToAttach.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &swapToAttach);

        
        lveRenderer.beginSwapChainRenderPass(cmd);
        ui.Draw(lveWindow, camera, registry, cmd);
        lveRenderer.endSwapChainRenderPass(cmd); });

                
                
                
                std::vector<VkImageMemoryBarrier> finalBarriers(1);
                finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                finalBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                finalBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                finalBarriers[0].image = hdrOutputTexture->getImage();
                finalBarriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                finalBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                finalBarriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

                renderGraph.addPass("Reset Layouts", finalBarriers, [](VkCommandBuffer cmd) {});

                
                renderGraph.execute(commandBuffer);

                lveRenderer.endFrame();
            }
        }

        vkDeviceWaitIdle(lveDevice.device());
    }
    void FirstApp::initCompute(VkDescriptorSetLayout globalSetLayout)
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
            globalSetLayout,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            shadowLayoutPtr->getDescriptorSetLayout(), 
            lightLayoutPtr->getDescriptorSetLayout(),  
            outputLayoutPtr->getDescriptorSetLayout(),
            irradianceLayoutPtr->getDescriptorSetLayout(),
            gtaoLayoutPtr->getDescriptorSetLayout()};

        lightingSystem = std::make_unique<DeferredLightingSystem>(lveDevice, computeLayouts);

        rcSystem = std::make_unique<RadianceCascadesSystem>(
            lveDevice,
            lveWindow.getExtent(),
            *globalPool,
            globalSetLayout,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            hdrOutputTexture->getImageView(),
            hdrOutputTexture->getSampler());
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
            globalSetLayout,                        
            gtaoLayoutPtr->getDescriptorSetLayout() 
        };
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
        void FirstApp::loadGameObjects(entt::registry& registry) {
            std::shared_ptr<BurnhopeTexture> diffuseTexture =
                BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/diffuse3.bhtex");

            
            std::shared_ptr<BurnhopeTexture> normalTexture =
                BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/normal3.bhtex");
                
            std::shared_ptr<BurnhopeTexture> rougnessTexture =
                BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/rougness3.bhtex");
                
            std::shared_ptr<BurnhopeTexture> metallicTexture =
                BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/metallic3.bhtex");
                
            std::shared_ptr<BurnhopeTexture> aoTexture =
                BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/ao3.bhtex");
                
            std::shared_ptr<BurnhopeTexture> heightTexture =
                BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/height3.bhtex");
            std::shared_ptr<BurnhopeModel> lveModel =
                BurnhopeModel::createModelFromFile(lveDevice, "models/cube.bhmesh");

    

            std::shared_ptr<Material> material = std::make_shared<Material>();
            material->setAlbedo(diffuseTexture);
            material->setAO(aoTexture);
            material->setMetallic(metallicTexture);
            material->setNormal(normalTexture);
            material->setRoughness(rougnessTexture);
            material->setHeight(heightTexture);
            auto cubeEntity = registry.create();
            registry.emplace<TagComponent>(cubeEntity, "Vulkan Cube");

            auto& transform = registry.emplace<TransformComponent>(cubeEntity);
            transform.transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
            transform.transform.scale = glm::vec3(5.0f, 0.5f, 5.0f);
            glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.transform.position);
            transform.transform.matrix = glm::scale(translation, transform.transform.scale);

            auto& mesh = registry.emplace<MeshComponent>(cubeEntity);
            mesh.model = lveModel;
            mesh.materials.push_back(material);
            mesh.materials.push_back(material);


            auto cubeEntity2 = registry.create();
            registry.emplace<TagComponent>(cubeEntity2, "Vulkan Cube");

            auto& transform2 = registry.emplace<TransformComponent>(cubeEntity2);
            transform2.transform.position = glm::vec3(0.0f, 3.0f, 0.0f);

            transform2.transform.matrix = glm::translate(glm::mat4(1.0f), transform2.transform.position);

            auto& mesh2 = registry.emplace<MeshComponent>(cubeEntity2);
            mesh2.model = lveModel;
            mesh2.materials.push_back(material);
     mesh2.materials.push_back(material);


            auto sunEntity = registry.create();
            registry.emplace<TagComponent>(sunEntity, "Sun");

            auto& sunTransform = registry.emplace<TransformComponent>(sunEntity);
            sunTransform.transform.rotation = glm::vec3(-45.0f, 30.0f, 0.0f); 

            auto& sunLight = registry.emplace<LightComponent>(sunEntity);
            sunLight.light.enable = true;
            sunLight.light.type = LightType::Directional;
            sunLight.light.color = glm::vec3(1.0f, 0.95f, 0.8f); 
            sunLight.light.intensity = 50.0f;
            sunLight.light.castShadows = true;
            sunLight.light.mobility = LightMobility::Movable;

            
            
            
            auto pointEntity = registry.create();
            registry.emplace<TagComponent>(pointEntity, "PointLight_1");

            auto& ptTransform = registry.emplace<TransformComponent>(pointEntity);
            ptTransform.transform.position = glm::vec3(2.0f, 1.0f, 0.0f);

            auto& ptLight = registry.emplace<LightComponent>(pointEntity);
            ptLight.light.enable = true;
            ptLight.light.type = LightType::Point;
            ptLight.light.color = glm::vec3(1.0f, 0.4f, 0.1f); 
            ptLight.light.intensity = 20.0f;
            ptLight.light.radius = 500.0f;
            ptLight.light.castShadows = true; 
            ptLight.light.mobility = LightMobility::Movable;


        }
    } 