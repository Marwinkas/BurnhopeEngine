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
#include "Render/RenderGraph.hpp"
namespace burnhope
{
    struct ProbeData {
        glm::vec4 positionAndRadius;
    };
    struct ProbesInfo {
        int count;
        ProbeData data[16];
    };

    class RTReflectionSystem {
    public:
        std::unique_ptr<BurnhopeTexture> rtReflectionsTexture;
        std::vector<std::unique_ptr<BurnhopeTexture>> probeTextures;
        std::unique_ptr<BurnhopeBuffer> probesBuffer;
        
        std::unique_ptr<BurnhopeDescriptorSetLayout> rtLayoutPtr;
        std::unique_ptr<BurnhopeDescriptorSetLayout> probeRenderLayoutPtr;
        
        VkDescriptorSet rtSet = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> probeRenderSets;

        std::unique_ptr<ComputeShader> rtReflectionsShader;
        std::unique_ptr<ComputeShader> probeRenderShader;

        BurnhopeDevice& device;
        BurnhopeDescriptorPool& pool;

        RTReflectionSystem(BurnhopeDevice& dev, BurnhopeDescriptorPool& pl) : device(dev), pool(pl) {}

        void init(VkExtent2D extent, VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout gBufferLayout, VkDescriptorSetLayout rtTLASLayout, VkDescriptorSetLayout storageLayout, VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout giLayout) {
            rtReflectionsTexture = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{extent.width, extent.height, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
            rtReflectionsTexture->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

            probesBuffer = std::make_unique<BurnhopeBuffer>(device, sizeof(ProbesInfo), 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            probesBuffer->map();

            rtLayoutPtr = BurnhopeDescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, 16)
                .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                .build();

            probeRenderLayoutPtr = BurnhopeDescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                .build();

            std::vector<VkDescriptorSetLayout> rtLayouts = {globalSetLayout, gBufferLayout, rtTLASLayout, storageLayout, textureLayout, rtLayoutPtr->getDescriptorSetLayout(), giLayout};
            rtReflectionsShader = std::make_unique<ComputeShader>(device, "shaders/rt_reflections.comp.spv", rtLayouts, sizeof(RCPushConstants));

            std::vector<VkDescriptorSetLayout> probeLayouts = {globalSetLayout, rtTLASLayout, storageLayout, textureLayout, probeRenderLayoutPtr->getDescriptorSetLayout()};
            probeRenderShader = std::make_unique<ComputeShader>(device, "shaders/probe_render.comp.spv", probeLayouts, sizeof(glm::vec4) + sizeof(int));
        }

        void updateDescriptors(VkExtent2D extent, std::shared_ptr<BurnhopeTexture> defaultWhiteTex) {
            if (!rtReflectionsTexture || rtReflectionsTexture->getExtent().width != extent.width || rtReflectionsTexture->getExtent().height != extent.height) {
                rtReflectionsTexture = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{extent.width, extent.height, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
                rtReflectionsTexture->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            }

            std::vector<VkDescriptorImageInfo> probeInfos;
            for (int i = 0; i < 16; i++) {
                if (i < probeTextures.size() && probeTextures[i]) {
                    auto info = probeTextures[i]->getImageInfo();
                    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    probeInfos.push_back(info);
                } else {
                    probeInfos.push_back(defaultWhiteTex->getImageInfo());
                }
            }

            auto imgInfo = rtReflectionsTexture->getImageInfo();
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            auto bufInfo = probesBuffer->descriptorInfo();

            vkDeviceWaitIdle(device.device());

            if (rtSet != VK_NULL_HANDLE) {
                std::vector<VkDescriptorSet> toFree = {rtSet};
                pool.freeDescriptors(toFree);
            }

            BurnhopeDescriptorWriter(*rtLayoutPtr, pool).writeImage(0, &imgInfo).writeImageArray(1, probeInfos).writeBuffer(2, &bufInfo).build(rtSet);
        }
    };
    std::unique_ptr<RTReflectionSystem> globalRTReflectionSystem;

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

        gBuffer = std::make_unique<BurnhopeGBuffer>(lveDevice, lveWindow.getExtent());

        simpleRenderSystem = std::make_unique<GeometryRenderSystem>(
            lveDevice,
            gBuffer->getRenderPass(),
            globalSetLayout->getDescriptorSetLayout());

        initCompute(globalSetLayout->getDescriptorSetLayout());
uiManager = std::make_unique<UIManager>(
            lveWindow, 
            lveDevice, 
            lveRenderer.getSwapChainRenderPass(), 
            &registry, 
            std::filesystem::current_path().string() // Путь к проекту
        );
        loadGameObjects(registry);
    }
    FirstApp::~FirstApp()
    {
        vkDeviceWaitIdle(lveDevice.device());

         uiManager.reset();
        ssgiSystem.reset();
        hizSystem.reset();
        rcSystem.reset();
        simpleRenderSystem.reset();
        gtaoSystem.reset();
        gtaoOutputTexture.reset();
        cullingSystem.reset();
        lightingSystem.reset();
        shadowSystem.reset();
        lightUboBuffer.reset();
        ssgiRawTexture.reset();
        postProcessTexture.reset();
        gBuffer.reset();
        hdrOutputTexture.reset();
        objectBuffer.reset();
        materialBuffer.reset();
        faceMatricesBuffer.reset();
        globalPool.reset();
        gBufferLayoutPtr.reset();
        outputLayoutPtr.reset();
        ssgiLayoutPtr.reset();
        postProcessLayoutPtr.reset();
        shadowLayoutPtr.reset();
        lightLayoutPtr.reset();
        dummyGridBuffer.reset();
        dummyIndexBuffer.reset();
        globalRTReflectionSystem.reset();
    }
    glm::mat4 shadowPerspective(float fovY, float aspect, float zNear, float zFar)
    {
        glm::mat4 proj = glm::perspective(glm::radians(fovY), aspect, zNear, zFar);
        proj[1][1] *= -1.0f;
        return proj;
    }
    void FirstApp::RebuildBatches(entt::registry &registry, GeometryRenderSystem &renderSystem)
    {
        if (storageSet != VK_NULL_HANDLE) {
            std::vector<VkDescriptorSet> toFree = {storageSet};
            globalPool->freeDescriptors(toFree);
            storageSet = VK_NULL_HANDLE;
        }
        if (textureSet != VK_NULL_HANDLE) {
            std::vector<VkDescriptorSet> toFree = {textureSet};
            globalPool->freeDescriptors(toFree);
            textureSet = VK_NULL_HANDLE;
        }
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
                    matData.hasEmissive = currentMat->hasEmissive ? 1 : 0;
                    matData.emissiveIntensity = currentMat->emissiveIntensity;
                    matData.useORM = currentMat->isORM ? 1 : 0;
                    matData.albedoIdx = getTexIndex(currentMat->albedoMap, defaultWhiteIdx);
                    matData.normalIdx = getTexIndex(currentMat->normalMap, defaultNormalIdx);
                    matData.roughnessIdx = getTexIndex(currentMat->roughnessMap, defaultWhiteIdx);
                    matData.metallicIdx = getTexIndex(currentMat->metallicMap, defaultWhiteIdx);
                    matData.aoIdx = getTexIndex(currentMat->aoMap, defaultWhiteIdx);
                    matData.emissiveIdx = getTexIndex(currentMat->emissiveMap, defaultWhiteIdx);
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
                obj.indexBufferAddress = meshComp.model->getIndexBufferAddress() + subMeshes[i].firstIndices[0] * sizeof(uint32_t);
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
        glm::mat4 prevViewProj = glm::mat4(1.0f);
        float timeAccumulator = 0.0f;
        while (!lveWindow.shouldClose())
        {
            glfwPollEvents();
            
            uint32_t currentSubMeshCount = 0;
            registry.view<MeshComponent>().each([&](const MeshComponent &meshComp) {
                if (meshComp.model && meshComp.isVisible) {
                    currentSubMeshCount += meshComp.model->getSubMeshes().size();
                }
            });

            if (currentSubMeshCount != totalSubMeshCount) {
                vkDeviceWaitIdle(lveDevice.device());
                if (cullingSystem) cullingSystem.reset(); 
                
                RebuildBatches(registry, *simpleRenderSystem); 
                buildTLAS(registry);                           

                VkWriteDescriptorSetAccelerationStructureKHR asInfo{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
                asInfo.accelerationStructureCount = 1;
                asInfo.pAccelerationStructures = &tlasHandle;
                VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                descriptorWrite.dstSet = rtSet;
                descriptorWrite.dstBinding = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pNext = &asInfo;
                vkUpdateDescriptorSets(lveDevice.device(), 1, &descriptorWrite, 0, nullptr);

                if (shadowObjectSet != VK_NULL_HANDLE) {
                    std::vector<VkDescriptorSet> toFree = {shadowObjectSet};
                    globalPool->freeDescriptors(toFree);
                }
                if (objectBuffer) {
                    auto objInfo = objectBuffer->descriptorInfo();
                    BurnhopeDescriptorWriter(*shadowObjectLayoutPtr, *globalPool).writeBuffer(0, &objInfo).build(shadowObjectSet);
                }
            }

            bool transformsChanged = false;

            registry.view<TransformComponent>().each([&](entt::entity entity, TransformComponent &tComp)
                                                     {
                if (tComp.transform.updatematrix) {
                    tComp.transform.updateMatrixIfNeeded();
                    transformsChanged = true;
                    if (registry.any_of<ReflectionProbeComponent>(entity)) {
                        registry.get<ReflectionProbeComponent>(entity).updateNeeded = true;
                    }
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
                
                vkDeviceWaitIdle(lveDevice.device()); 
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
                ssgiRawTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                     VkExtent3D{newExtent.width, newExtent.height, 1},
                                                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                                     VK_SAMPLE_COUNT_1_BIT);
                ssgiRawTexture->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                rebuildGBufferDescriptorSets();
                if (rcSystem)
                {
                    rcSystem->rebuildOnResize(newExtent, hdrOutputTexture->getImageView(), hdrOutputTexture->getSampler());
                }
                if (globalRTReflectionSystem) {
                    globalRTReflectionSystem->updateDescriptors(newExtent, defaultWhiteTex);
                }
                continue;
            }
            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            currentTime = newTime;
            frameCount++;
            timeAccumulator += frameTime;
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

                // --- ЧИТАЕМ НАСТРОЙКИ ИЗ UIMANAGER ---
                const auto& rs = uiManager->GetContext().renderSettings;

                ubo.sscsParams = glm::vec4(rs.enableContactShadows ? 1.0f : 0.0f, rs.contactShadowLength, (float)rs.contactShadowSteps, rs.contactShadowThickness);
                ubo.gtaoParams = glm::vec4(rs.enableSSAO ? 1.0f : 0.0f, rs.ssaoRadius, rs.ssaoBias, rs.ssaoIntensity);
                ubo.fogParams = glm::vec4(rs.enableFog ? 1.0f : 0.0f, rs.fogDensity, rs.fogHeightFalloff, rs.fogBaseHeight);
                ubo.fogColor = glm::vec4(rs.fogColor[0], rs.fogColor[1], rs.fogColor[2], rs.inscatterIntensity);
                ubo.inscatterColor = glm::vec4(rs.inscatterColor[0], rs.inscatterColor[1], rs.inscatterColor[2], rs.inscatterPower);
                ubo.skyZenithColor = glm::vec4(rs.skyZenithColor[0], rs.skyZenithColor[1], rs.skyZenithColor[2], 1.0f);
                ubo.skyHorizonColor = glm::vec4(rs.skyHorizonColor[0], rs.skyHorizonColor[1], rs.skyHorizonColor[2], 1.0f);
                ubo.skySunParams = glm::vec4(rs.sunSize, rs.sunGlow, rs.sunGlowSize, 0.0f);
                ubo.ssgiParams = glm::vec4(rs.enableSSGI ? 1.0f : 0.0f, (float)rs.ssgiRayCount, rs.ssgiStepSize, rs.ssgiThickness);
                ubo.rtParams = glm::vec4(rs.enableRTReflections ? 1.0f : 0.0f, (float)rs.rtMaxBounces, rs.enableRadianceCascades ? 1.0f : 0.0f, 0.0f);

                ubo.prevViewProj = prevViewProj;
                ubo.ppExposureParams = glm::vec4(rs.autoExposure ? 1.0f : 0.0f, rs.manualExposure, rs.minBrightness, rs.maxBrightness);
                ubo.ppColorBalance = glm::vec4(rs.temperature, rs.contrast, rs.saturation, rs.gamma);
                ubo.ppBloomParams = glm::vec4(rs.enableBloom ? 1.0f : 0.0f, rs.bloomThreshold, rs.bloomIntensity, (float)rs.bloomBlurIterations);
                ubo.ppDoFParams = glm::vec4(rs.enableDoF ? 1.0f : 0.0f, rs.focusDistance, rs.focusRange, rs.bokehSize);
                ubo.ppVignetteGrain = glm::vec4(rs.enableVignette ? rs.vignetteIntensity : 0.0f, rs.enableFilmGrain ? rs.grainIntensity : 0.0f, rs.enableSharpen ? rs.sharpenIntensity : 0.0f, rs.enableChromaticAberration ? rs.caIntensity : 0.0f);
                ubo.ppMotionBlur = glm::vec4(rs.enableMotionBlur ? 1.0f : 0.0f, rs.mbStrength, timeAccumulator, 0.0f);
                ubo.ppLensFlare = glm::vec4(rs.enableLensFlares ? 1.0f : 0.0f, rs.flareIntensity, rs.ghostDispersal, (float)rs.ghosts);
                prevViewProj = ubo.projection * ubo.view;

                auto cascadeMats = shadowSystem->getCSM()->calculateMatrices(
                    camera, shadowSystem->getSunDir(),
                    {shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]});
                for (int i = 0; i < 4; i++)
                    ubo.sunLightSpaceMatrices[i] = cascadeMats[i];
                ubo.cascadeSplits = glm::vec4(shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();

                ProbesInfo pInfo{};
                pInfo.count = 0;
                registry.view<TransformComponent, ReflectionProbeComponent>().each([&](entt::entity e, TransformComponent& t, ReflectionProbeComponent& p) {
                    if (pInfo.count >= 16) return;
                    if (p.textureIndex == -1) {
                        vkDeviceWaitIdle(lveDevice.device()); 
                        p.textureIndex = globalRTReflectionSystem->probeTextures.size();
                        auto tex = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{(uint32_t)p.resolution, (uint32_t)p.resolution, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
                        tex->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                        VkDescriptorSet newSet;
                        auto imgInfo = tex->getImageInfo();
                        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                        BurnhopeDescriptorWriter(*globalRTReflectionSystem->probeRenderLayoutPtr, *globalPool).writeImage(0, &imgInfo).build(newSet);
                        globalRTReflectionSystem->probeTextures.push_back(std::move(tex));
                        globalRTReflectionSystem->probeRenderSets.push_back(newSet);
                        globalRTReflectionSystem->updateDescriptors(extent, defaultWhiteTex);
                    }
                    if (t.transform.updatematrix || p.updateNeeded) {
                        p.updateNeeded = true; 
                    }
                    pInfo.data[pInfo.count].positionAndRadius = glm::vec4(t.transform.position, p.radius);
                    pInfo.count++;
                });
                if (globalRTReflectionSystem->probesBuffer) {
                    globalRTReflectionSystem->probesBuffer->writeToBuffer(&pInfo);
                    globalRTReflectionSystem->probesBuffer->flush();
                }

                renderPipeline.clear();
                renderPipeline.addPass("Shadow Maps Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1)}, [&](VkCommandBuffer cmd)
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
                renderPipeline.addPass("G-Buffer Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    VkRenderPassBeginInfo renderPassInfo{};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; renderPassInfo.renderPass = gBuffer->getRenderPass();
                    renderPassInfo.framebuffer = gBuffer->getFramebuffer(); renderPassInfo.renderArea.extent = extent;
                    std::array<VkClearValue, 5> clearValues{}; clearValues[4].depthStencil = { 1.0f, 0 };
                    renderPassInfo.clearValueCount = (uint32_t)clearValues.size(); renderPassInfo.pClearValues = clearValues.data();
                    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
                    VkViewport viewport{}; viewport.width = (float)extent.width; viewport.height = (float)extent.height; viewport.maxDepth = 1.0f;
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    VkRect2D scissor{ {0, 0}, extent }; vkCmdSetScissor(cmd, 0, 1, &scissor);
                    simpleRenderSystem->renderEntities(frameInfo, registry, storageSet, textureSet, *cullingSystem, totalSubMeshCount);
                    vkCmdEndRenderPass(cmd); });
                renderPipeline.addPass("Hi-Z Pass", {RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)}, [&](VkCommandBuffer cmd)
                                       { hizSystem->compute(cmd, extent); });
                renderPipeline.addPass("GTAO Pass", {RenderPipeline::createImageBarrier(gtaoOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       { gtaoSystem->compute(cmd, globalDescriptorSets[frameIndex], gtaoSet, extent.width, extent.height); });
                renderPipeline.addPass("Radiance Cascades GI", {
                    RenderPipeline::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                    RenderPipeline::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                    RenderPipeline::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                    RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)
                },
                                       [&](VkCommandBuffer cmd)
                                       {
                    glm::vec3 sceneMin(-8.0f, -8.0f, -8.0f);
                    glm::vec3 sceneMax( 8.0f,  8.0f,  8.0f);
                    rcSystem->dispatch(cmd, globalDescriptorSets[frameIndex], gBufferSet, ubo.invViewProj, camera.Position, sceneMin, sceneMax, extent, rtSet, storageSet, textureSet); });

                renderPipeline.addPass("Probe Update", {}, [&](VkCommandBuffer cmd) {
                    registry.view<TransformComponent, ReflectionProbeComponent>().each([&](entt::entity e, TransformComponent& t, ReflectionProbeComponent& p) {
                        if (p.updateNeeded && p.textureIndex != -1) {
                            p.updateNeeded = false;
                            globalRTReflectionSystem->probeRenderShader->bind(cmd);
                            struct Push { glm::vec4 pos; int res; } pushData;
                            pushData.pos = glm::vec4(t.transform.position, 1.0f);
                            pushData.res = p.resolution;
                            globalRTReflectionSystem->probeRenderShader->pushConstants(cmd, &pushData, sizeof(Push));
                            globalRTReflectionSystem->probeRenderShader->bindDescriptorSets(cmd, {
                                globalDescriptorSets[frameIndex], rtSet, storageSet, textureSet, globalRTReflectionSystem->probeRenderSets[p.textureIndex]
                            });
                            globalRTReflectionSystem->probeRenderShader->dispatch(cmd, (p.resolution + 7) / 8, (p.resolution + 7) / 8, 1);
                        }
                    });
                });

                renderPipeline.addPass("RT Reflections", {
                    RenderPipeline::createImageBarrier(globalRTReflectionSystem->rtReflectionsTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
                }, [&](VkCommandBuffer cmd) {
                    globalRTReflectionSystem->rtReflectionsShader->bind(cmd);
                    
                    glm::vec3 sceneMin(-8.0f, -8.0f, -8.0f);
                    glm::vec3 sceneMax( 8.0f,  8.0f,  8.0f);
                    
                    RCPushConstants rcPush{};
                    rcPush.probeGridMin = glm::vec4(sceneMin, 0.0f);
                    rcPush.probeGridMax = glm::vec4(sceneMax, 0.0f);
                    rcPush.probeCount   = glm::ivec4(rs.rcProbeGridX, rs.rcProbeGridY, rs.rcProbeGridZ, 0);
                    rcPush.params = glm::vec4(
                        rs.rcBaseRayLength, 
                        (float)extent.width, 
                        (float)extent.height, 
                        (float)rs.rcOctaSize
                    );
                    
                    globalRTReflectionSystem->rtReflectionsShader->pushConstants(cmd, &rcPush, sizeof(RCPushConstants));

                    globalRTReflectionSystem->rtReflectionsShader->bindDescriptorSets(cmd, {
                        globalDescriptorSets[frameIndex], gBufferSet, rtSet, storageSet, textureSet, globalRTReflectionSystem->rtSet, rcSystem->getGISet()
                    });
                    globalRTReflectionSystem->rtReflectionsShader->dispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
                });

                renderPipeline.addPass("Compute Lighting", {}, [&](VkCommandBuffer cmd)
                                       {
                    std::vector<VkDescriptorSet> computeSets = { globalDescriptorSets[frameIndex], gBufferSet, shadowSet, lightSet, computeOutputSet, rcSystem->getGISet(), gtaoSet, rtSet, globalRTReflectionSystem->rtSet };
                    lightingSystem->computeLighting(cmd, computeSets, extent.width, extent.height); });

                renderPipeline.addPass("SSGI Pass", {
                    RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                    RenderPipeline::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
                }, [&](VkCommandBuffer cmd) {
                    ssgiSystem->computeSSGI(cmd, globalDescriptorSets[frameIndex], gBufferSet, shadowSet, ssgiSet, extent.width, extent.height);
                });
                renderPipeline.addPass("SSGI Denoise Pass", {
                    RenderPipeline::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
                }, [&](VkCommandBuffer cmd) {
                    ssgiSystem->computeDenoise(cmd, globalDescriptorSets[frameIndex], gBufferSet, ssgiSet, extent.width, extent.height);
                });
        renderPipeline.addPass("Post Processing Pass", {
            RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
            RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
        }, [&](VkCommandBuffer cmd) {
            postProcessShader->bind(cmd);
            postProcessShader->bindDescriptorSets(cmd, { globalDescriptorSets[frameIndex], postProcessSet });
            postProcessShader->dispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);
        });
                VkImage swapChainImage = lveRenderer.getCurrentSwapChainImage();
         renderPipeline.addPass("Blit and UI", {RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    VkImageMemoryBarrier swapToDst = RenderPipeline::createImageBarrier(swapChainImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapToDst);
                    VkImageBlit blit{};
                    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.dstOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
            vkCmdBlitImage(cmd, postProcessTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
                    VkImageMemoryBarrier swapToAttach = RenderPipeline::createImageBarrier(swapChainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &swapToAttach);
                    
                    lveRenderer.beginSwapChainRenderPass(cmd);
                    // --- ОБНОВЛЕННЫЙ ВЫЗОВ UI ---
                    uiManager->Draw(lveWindow, camera, cmd);
                    lveRenderer.endSwapChainRenderPass(cmd); });
        renderPipeline.addPass("Reset Layouts", {RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [](VkCommandBuffer cmd) {});
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
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT| VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
        ssgiRawTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
        ssgiRawTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);

        postProcessTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT);
        postProcessTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);

        gBufferLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Emissive
                               .build();
        outputLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();
        postProcessLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                              .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                              .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();
        ssgiLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT) // hdrOutput
                              .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT) // ssgiRaw
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
        giLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                  .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // Diffuse GI
                                  .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // Specular GI
                                  .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // cascade0
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

        VkDescriptorImageInfo emissiveInfo{};
        emissiveInfo.sampler = gBuffer->getEmissive()->getSampler();
        emissiveInfo.imageView = gBuffer->getEmissive()->getImageView();
        emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
            .writeImage(0, &normInfo)
            .writeImage(1, &albInfo)
            .writeImage(2, &extraInfo)
            .writeImage(3, &depthInfo)
            .writeImage(4, &emissiveInfo)
            .build(gBufferSet);
        VkDescriptorImageInfo outImgInfo{};
        outImgInfo.imageView = hdrOutputTexture->getImageView();
        outImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*outputLayoutPtr, *globalPool)
            .writeImage(0, &outImgInfo)
            .build(computeOutputSet);

        VkDescriptorImageInfo ppOutInfo{};
        ppOutInfo.imageView = postProcessTexture->getImageView();
        ppOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo ppInInfo{};
        ppInInfo.sampler = hdrOutputTexture->getSampler();
        ppInInfo.imageView = hdrOutputTexture->getImageView();
        ppInInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*postProcessLayoutPtr, *globalPool)
            .writeImage(0, &ppOutInfo)
            .writeImage(1, &ppInInfo)
            .writeImage(2, &depthInfo)
            .build(postProcessSet);
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
        if (std::filesystem::exists("../textures/bluenoise.png")) {
            blueNoiseTex = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/bluenoise.png");
            std::cout << "yes";
        } else {
            blueNoiseTex = defaultWhiteTex;
        }
        VkDescriptorImageInfo csmInfo{};
        csmInfo.sampler = shadowSystem->getCSM()->getTexture()->getSampler();
        csmInfo.imageView = csmArrayView;
        csmInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        auto atlasInfo = shadowSystem->getAtlas()->getTexture()->getImageInfo();
        auto noiseInfo = blueNoiseTex->getImageInfo();
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

        globalRTReflectionSystem = std::make_unique<RTReflectionSystem>(lveDevice, *globalPool);
        globalRTReflectionSystem->init(lveWindow.getExtent(), globalSetLayouts, gBufferLayoutPtr->getDescriptorSetLayout(), rtLayoutPtr->getDescriptorSetLayout(), simpleRenderSystem->getRenderSystemLayout()->getDescriptorSetLayout(), simpleRenderSystem->getTextureLayout()->getDescriptorSetLayout(), giLayoutPtr->getDescriptorSetLayout());
        globalRTReflectionSystem->updateDescriptors(lveWindow.getExtent(), defaultWhiteTex);

        std::vector<VkDescriptorSetLayout> computeLayouts = {
            globalSetLayouts,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            shadowLayoutPtr->getDescriptorSetLayout(),
            lightLayoutPtr->getDescriptorSetLayout(),
            outputLayoutPtr->getDescriptorSetLayout(),
            giLayoutPtr->getDescriptorSetLayout(),
            gtaoLayoutPtr->getDescriptorSetLayout(), rtLayoutPtr->getDescriptorSetLayout(),
            globalRTReflectionSystem->rtLayoutPtr->getDescriptorSetLayout()};
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

        std::vector<VkDescriptorSetLayout> ppLayouts = { globalSetLayouts, postProcessLayoutPtr->getDescriptorSetLayout() };
        postProcessShader = std::make_unique<ComputeShader>(lveDevice, "shaders/post_process.comp.spv", ppLayouts);

        VkDescriptorImageInfo ssgiRawInfo{};
        ssgiRawInfo.imageView = ssgiRawTexture->getImageView();
        ssgiRawInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*ssgiLayoutPtr, *globalPool)
            .writeImage(0, &outImgInfo)
            .writeImage(1, &ssgiRawInfo)
            .build(ssgiSet);

        ssgiSystem = std::make_unique<SSGISystem>(
            lveDevice,
            globalSetLayouts,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            shadowLayoutPtr->getDescriptorSetLayout(),
            ssgiLayoutPtr->getDescriptorSetLayout()
        );
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
        if (ssgiSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {ssgiSet};
            globalPool->freeDescriptors(toFree);
            ssgiSet = VK_NULL_HANDLE;
        }
        if (postProcessSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {postProcessSet};
            globalPool->freeDescriptors(toFree);
        }
        auto normInfo = gBuffer->getNormalRoughness()->getImageInfo();
        auto albInfo = gBuffer->getAlbedoMetallic()->getImageInfo();
        auto extraInfo = gBuffer->getHeightAO()->getImageInfo();
        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = gBuffer->getDepth()->getSampler();
        depthInfo.imageView = gBuffer->getDepth()->getImageView();
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo emissiveInfo{};
        emissiveInfo.sampler = gBuffer->getEmissive()->getSampler();
        emissiveInfo.imageView = gBuffer->getEmissive()->getImageView();
        emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        bool ok = BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
                      .writeImage(0, &normInfo)
                      .writeImage(1, &albInfo)
                      .writeImage(2, &extraInfo)
                      .writeImage(3, &depthInfo)
                      .writeImage(4, &emissiveInfo)
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
        
        postProcessTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT);
        postProcessTexture->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorImageInfo ppOutInfo{}; ppOutInfo.imageView = postProcessTexture->getImageView(); ppOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo ppInInfo{}; ppInInfo.sampler = hdrOutputTexture->getSampler(); ppInInfo.imageView = hdrOutputTexture->getImageView(); ppInInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*postProcessLayoutPtr, *globalPool)
            .writeImage(0, &ppOutInfo)
            .writeImage(1, &ppInInfo)
            .writeImage(2, &depthInfo)
            .build(postProcessSet);
        VkDescriptorImageInfo ssgiRawInfo{};
        ssgiRawInfo.imageView = ssgiRawTexture->getImageView();
        ssgiRawInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*ssgiLayoutPtr, *globalPool)
                 .writeImage(0, &outImgInfo)
                 .writeImage(1, &ssgiRawInfo)
                 .build(ssgiSet);
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
            instance.instanceCustomIndex = customIndex;   
            instance.mask = 0xFF;                         
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = meshComp.model->getBLASAddress();

            instances.push_back(instance);
            
            customIndex += meshComp.model->getSubMeshes().size();
        }

        if (instances.empty())
            return; 

        VkDeviceSize instancesBufferSize = instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        instancesBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(VkAccelerationStructureInstanceKHR),
            instances.size(),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 
        );

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
        std::shared_ptr<BurnhopeTexture> diffuseTexture = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/diffuse3.png");
        std::shared_ptr<BurnhopeTexture> normalTexture = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/normal3.png");
        std::shared_ptr<BurnhopeTexture> rougnessTexture = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/rougness3.png");
        std::shared_ptr<BurnhopeTexture> metallicTexture = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/metallic3.png");
        std::shared_ptr<BurnhopeTexture> aoTexture = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/ao3.png");
        std::shared_ptr<BurnhopeTexture> heightTexture = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/height3.png");

        std::shared_ptr<BurnhopeModel> lveModel = BurnhopeModel::createModelFromFile(lveDevice, "models/cube.bhmesh");

        std::shared_ptr<Material> material = std::make_shared<Material>();
        material->setAlbedo(diffuseTexture);
        material->setAO(aoTexture);
        material->setMetallic(metallicTexture);
        material->setNormal(normalTexture);
        material->setRoughness(rougnessTexture);
        material->setHeight(heightTexture);


        std::shared_ptr<BurnhopeTexture> diffuseTexture2 = BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/Titanium-Scuffed_basecolor.png");
        std::shared_ptr<BurnhopeTexture> normalTexture2 = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/Titanium-Scuffed_normal.png");
        std::shared_ptr<BurnhopeTexture> rougnessTexture2 = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/Titanium-Scuffed_roughness.png");
        std::shared_ptr<BurnhopeTexture> metallicTexture2 = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/Titanium-Scuffed_metallic.png");


        std::shared_ptr<Material> material2 = std::make_shared<Material>();
        material2->setAlbedo(diffuseTexture2);
        material2->setMetallic(metallicTexture2);
        material2->setNormal(normalTexture2);
        material2->setRoughness(rougnessTexture2);

        auto createBox = [&](glm::vec3 pos, glm::vec3 scale, std::string tag)
        {
            auto entity = registry.create();
            registry.emplace<IDComponent>(entity);
            registry.emplace<HierarchyComponent>(entity);
            registry.emplace<TagComponent>(entity, tag);

            auto &transform = registry.emplace<TransformComponent>(entity);
            transform.transform.position = pos;
            transform.transform.scale = scale;
            transform.transform.updateMatrixIfNeeded();

            auto &mesh = registry.emplace<MeshComponent>(entity);
            mesh.model = lveModel;
            mesh.materials.push_back(material);
            mesh.materials.push_back(material);
            return entity;
        };
        auto createBox2 = [&](glm::vec3 pos, glm::vec3 scale, std::string tag)
        {
            auto entity = registry.create();
            registry.emplace<IDComponent>(entity);
            registry.emplace<HierarchyComponent>(entity);
            registry.emplace<TagComponent>(entity, tag);

            auto &transform = registry.emplace<TransformComponent>(entity);
            transform.transform.position = pos;
            transform.transform.scale = scale;
            transform.transform.updateMatrixIfNeeded();

            auto &mesh = registry.emplace<MeshComponent>(entity);
            mesh.model = lveModel;
            mesh.materials.push_back(material2);
            mesh.materials.push_back(material2);
            return entity;
        };
        createBox({0.0f, 0.0f, 0.0f}, {5.0f, 0.1f, 5.0f}, "Floor");

        createBox({0.0f, 5.0f, 0.0f}, {5.0f, 0.1f, 5.0f}, "Ceiling");

        createBox({0.0f, 2.5f, -5.0f}, {5.0f, 2.5f, 0.1f}, "BackWall");

        createBox({-5.0f, 2.5f, 0.0f}, {0.1f, 2.5f, 5.0f}, "LeftWall");

        createBox({5.0f, 2.5f, 0.0f}, {0.1f, 2.5f, 5.0f}, "RightWall");

        createBox({-3.0f, 2.5f, 5.0f}, {2.0f, 2.5f, 0.1f}, "FrontWall_Left");
        createBox({3.0f, 2.5f, 5.0f}, {2.0f, 2.5f, 0.1f}, "FrontWall_Right");
        createBox({0.0f, 4.0f, 5.0f}, {1.0f, 1.0f, 0.1f}, "FrontWall_Top");

        auto testCube = createBox2({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, "TestCube");

        auto sunEntity = registry.create();
        registry.emplace<IDComponent>(sunEntity);
        registry.emplace<HierarchyComponent>(sunEntity);
        registry.emplace<TagComponent>(sunEntity, "Sun");

        auto &sunTransform = registry.emplace<TransformComponent>(sunEntity);
        sunTransform.transform.rotation = glm::vec3(70, 0, 0.0f);
        sunTransform.transform.updateMatrixIfNeeded();

        auto &sunLight = registry.emplace<LightComponent>(sunEntity);
        sunLight.light.enable = true;
        sunLight.light.type = LightType::Directional;
        sunLight.light.color = glm::vec3(1.0f, 0.95f, 0.8f); 
        sunLight.light.intensity = 1.0f;                     
        sunLight.light.castShadows = true;
        sunLight.light.mobility = LightMobility::Movable;

        auto probeEntity = registry.create();
        registry.emplace<IDComponent>(probeEntity);
        registry.emplace<HierarchyComponent>(probeEntity);
        registry.emplace<TagComponent>(probeEntity, "Reflection Probe");
        auto& probeTrans = registry.emplace<TransformComponent>(probeEntity);
        probeTrans.transform.position = glm::vec3(0.0f, 1.5f, 0.0f);
        probeTrans.transform.updateMatrixIfNeeded();
        registry.emplace<ReflectionProbeComponent>(probeEntity);
    }
}