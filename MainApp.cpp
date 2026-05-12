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
    struct ProbeData
    {
        glm::vec4 positionAndRadius;
    };
    struct ProbesInfo
    {
        int count;
        ProbeData data[16];
    };

    struct DecalDataGPU {
        glm::mat4 invModelMatrix;
        glm::vec4 params;
    };
    struct DecalBlock {
        int decalCount; int pad[3]; DecalDataGPU decals[1000];
    };

    class RTReflectionSystem
    {
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

        BurnhopeDevice &device;
        BurnhopeDescriptorPool &pool;

        RTReflectionSystem(BurnhopeDevice &dev, BurnhopeDescriptorPool &pl) : device(dev), pool(pl) {}

        void init(VkExtent2D extent, VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout gBufferLayout, VkDescriptorSetLayout rtTLASLayout, VkDescriptorSetLayout storageLayout, VkDescriptorSetLayout textureLayout, VkDescriptorSetLayout giLayout)
        {
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

        void updateDescriptors(VkExtent2D extent, std::shared_ptr<BurnhopeTexture> defaultWhiteTex)
        {
            if (!rtReflectionsTexture || rtReflectionsTexture->getExtent().width != extent.width || rtReflectionsTexture->getExtent().height != extent.height)
            {
                rtReflectionsTexture = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{extent.width, extent.height, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
                rtReflectionsTexture->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            }

            std::vector<VkDescriptorImageInfo> probeInfos;
            for (int i = 0; i < 16; i++)
            {
                if (i < probeTextures.size() && probeTextures[i])
                {
                    auto info = probeTextures[i]->getImageInfo();
                    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    probeInfos.push_back(info);
                }
                else
                {
                    probeInfos.push_back(defaultWhiteTex->getImageInfo());
                }
            }

            auto imgInfo = rtReflectionsTexture->getImageInfo();
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            auto bufInfo = probesBuffer->descriptorInfo();

            vkDeviceWaitIdle(device.device());

            if (rtSet != VK_NULL_HANDLE)
            {
                std::vector<VkDescriptorSet> toFree = {rtSet};
                pool.freeDescriptors(toFree);
            }

            BurnhopeDescriptorWriter(*rtLayoutPtr, pool).writeImage(0, &imgInfo).writeImageArray(1, probeInfos).writeBuffer(2, &bufInfo).build(rtSet);
        }
    };
    std::unique_ptr<RTReflectionSystem> globalRTReflectionSystem;

    class VolumetricSystem {
    public:
        std::unique_ptr<BurnhopeTexture> volumetricTex;
        std::unique_ptr<BurnhopeDescriptorSetLayout> writeLayoutPtr;
        VkDescriptorSet writeSet = VK_NULL_HANDLE;
        std::unique_ptr<ComputeShader> shader;
        BurnhopeDevice& device;
        BurnhopeDescriptorPool& pool;

        VolumetricSystem(BurnhopeDevice& dev, BurnhopeDescriptorPool& pl) : device(dev), pool(pl) {}

        void init(VkExtent2D extent, VkDescriptorSetLayout globalLayout, VkDescriptorSetLayout gBufferLayout, VkDescriptorSetLayout shadowLayout, VkDescriptorSetLayout lightLayout, VkDescriptorSetLayout vsmLayout, VkDescriptorSetLayout portalLayout) {
            VkExtent3D volExtent = { std::max(1u, extent.width / 2), std::max(1u, extent.height / 2), 1 };
            volumetricTex = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R16G16B16A16_SFLOAT, volExtent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
            volumetricTex->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

            writeLayoutPtr = BurnhopeDescriptorSetLayout::Builder(device).addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT).build();

            std::vector<VkDescriptorSetLayout> layouts = { globalLayout, gBufferLayout, shadowLayout, lightLayout, vsmLayout, writeLayoutPtr->getDescriptorSetLayout(), portalLayout };
            shader = std::make_unique<ComputeShader>(device, "shaders/volumetric.comp.spv", layouts);
        }

        void updateDescriptors(VkExtent2D extent) {
            VkExtent3D volExtent = { std::max(1u, extent.width / 2), std::max(1u, extent.height / 2), 1 };
            if (!volumetricTex || volumetricTex->getExtent().width != volExtent.width || volumetricTex->getExtent().height != volExtent.height) {
                volumetricTex = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R16G16B16A16_SFLOAT, volExtent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
                volumetricTex->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            }
            auto imgInfo = volumetricTex->getImageInfo(); imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            if (writeSet != VK_NULL_HANDLE) { std::vector<VkDescriptorSet> toFree = {writeSet}; pool.freeDescriptors(toFree); }
            BurnhopeDescriptorWriter(*writeLayoutPtr, pool).writeImage(0, &imgInfo).build(writeSet);
        }
    };
    std::unique_ptr<VolumetricSystem> globalVolumetricSystem;
    std::unique_ptr<BurnhopeDescriptorSetLayout> globalVolumetricReadLayout;
    VkDescriptorSet globalVolumetricReadSet = VK_NULL_HANDLE;
    std::unique_ptr<BurnhopeBuffer> globalDecalBuffer;

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

        if (std::filesystem::exists("../textures/lens_dirt.png")) {
            defaultDirtTex = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/lens_dirt.png");
        } else {
            defaultDirtTex = defaultWhiteTex;
        }
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

        if (tlasHandle != VK_NULL_HANDLE)
        {
            auto pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkDestroyAccelerationStructureKHR");
            if (pfnDestroyAccelerationStructureKHR)
            {
                pfnDestroyAccelerationStructureKHR(lveDevice.device(), tlasHandle, nullptr);
            }
        }
        tlasBuffer.reset();
        instancesBuffer.reset();

        uiManager.reset();
        ssgiSystem.reset();
        hizSystem.reset();
        taaHistoryTexture.reset();
        taaResolvedTexture.reset();
        rcSystem.reset();
        simpleRenderSystem.reset();
        gtaoSystem.reset();
        postProcessShader.reset();
        shadowRenderSystem.reset();
        gtaoOutputTexture.reset();
        cullingSystem.reset();
        lightingSystem.reset();
        exposureBuffer.reset();
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
        portalInfoLayoutPtr.reset();
        outputLayoutPtr.reset();
        ssgiLayoutPtr.reset();
        postProcessLayoutPtr.reset();
        shadowLayoutPtr.reset();
        lightLayoutPtr.reset();
        shadowObjectLayoutPtr.reset();
        dummyGridBuffer.reset();
        dummyIndexBuffer.reset();
        globalRTReflectionSystem.reset();
        portalUbosBuffer.reset();
        globalVolumetricSystem.reset();
        globalVolumetricReadLayout.reset();
        globalVolumetricReadSet = VK_NULL_HANDLE;
        globalDecalBuffer.reset();
        if (csmArrayView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(lveDevice.device(), csmArrayView, nullptr);
            csmArrayView = VK_NULL_HANDLE;
        }
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
        if (storageSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {storageSet};
            globalPool->freeDescriptors(toFree);
            storageSet = VK_NULL_HANDLE;
        }
        if (textureSet != VK_NULL_HANDLE)
        {
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

        auto decalView = registry.view<DecalComponent>();
    

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
            for (auto [entity, decal] : decalView.each()) {
            decal.albedoTexIdx = getTexIndex(decal.albedoTex, defaultWhiteIdx);
            decal.normalTexIdx = getTexIndex(decal.normalTex, defaultNormalIdx);
        }
        auto view = registry.view<TransformComponent, MeshComponent>();
        for (auto [entity, transformComp, meshComp] : view.each())
        {
            if (!meshComp.model || !meshComp.isVisible)
                continue;
            const auto &subMeshes = meshComp.model->getSubMeshes();
            for (uint32_t i = 0; i < subMeshes.size(); i++)
            {
                std::shared_ptr<Material> currentMat = (i < meshComp.materials.size() && meshComp.materials[i])
                                                           ? meshComp.materials[i]
                                                           : defaultWhiteMaterial;
                uint32_t currentMatID = 0;
                if (matToIndex.find(currentMat.get()) == matToIndex.end())
                {
                    currentMatID = globalMatIndex++;
                    matToIndex[currentMat.get()] = currentMatID;
                    MaterialData matData{};
                    matData.albedoIdx = getTexIndex(currentMat->albedoMap, defaultWhiteIdx);
                    matData.normalIdx = getTexIndex(currentMat->normalMap, defaultNormalIdx);
                    matData.heightIdx = getTexIndex(currentMat->heightMap, defaultWhiteIdx);
                    matData.metallicIdx = getTexIndex(currentMat->metallicMap, defaultWhiteIdx);
                    matData.roughnessIdx = getTexIndex(currentMat->roughnessMap, defaultWhiteIdx);
                    matData.aoIdx = getTexIndex(currentMat->aoMap, defaultWhiteIdx);
                    matData.emissiveIdx = getTexIndex(currentMat->emissiveMap, defaultWhiteIdx);

                    matData.hasAlbedo = currentMat->hasAlbedo ? 1 : 0;
                    matData.hasNormal = currentMat->hasNormal ? 1 : 0;
                    matData.hasHeight = currentMat->hasHeight ? 1 : 0;
                    matData.hasMetallic = currentMat->hasMetallic ? 1 : 0;
                    matData.hasRoughness = currentMat->hasRoughness ? 1 : 0;
                    matData.hasAO = currentMat->hasAO ? 1 : 0;
                    matData.hasEmissive = currentMat->hasEmissive ? 1 : 0;
                    matData.useTriplanar = currentMat->useTriplanar ? 1 : 0;
                    matData.triplanarScale = currentMat->triplanarScale;

                    matData.uvScale = currentMat->uvScale;
                    matData.emissiveIntensity = currentMat->emissiveIntensity;
                    matData.useORM = currentMat->isORM ? 1 : 0;

                    matData.albedoColor = glm::vec4(currentMat->albedoColor, 1.0f);
                    matData.emissiveColor = glm::vec4(currentMat->emissiveColor, 1.0f);
                    matData.metallicStrength = currentMat->metallicStrength;
                    matData.roughnessStrength = currentMat->roughnessStrength;
                    matData.normalStrength = currentMat->normalStrength;
                    matData.heightStrength = currentMat->heightStrength;
                    matData.aoStrength = currentMat->aoStrength; // Добавлено присвоение aoStrength
                    matData.repeatTexture = currentMat->repeatTexture ? 1 : 0;
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
        
        std::vector<ObjectData> uploadObjData = objDataList;
        std::vector<MaterialData> uploadMatData = matDataList;
        if (uploadObjData.empty()) uploadObjData.push_back(ObjectData{});
        if (uploadMatData.empty()) uploadMatData.push_back(MaterialData{});

        if (!cullingSystem)
        {
            cullingSystem = std::make_unique<CullingSystem>(lveDevice, static_cast<uint32_t>(uploadObjData.size()));
        }
        objectBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(ObjectData), uploadObjData.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        objectBuffer->map();
        objectBuffer->writeToBuffer(uploadObjData.data());
        cullingSystem->bindObjectBuffer(
            objectBuffer->getBuffer(),
            sizeof(ObjectData) * uploadObjData.size());

        materialBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(MaterialData), uploadMatData.size(),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        materialBuffer->map();
        materialBuffer->writeToBuffer(uploadMatData.data());

        auto objInfo = objectBuffer->descriptorInfo();
        auto matInfo = materialBuffer->descriptorInfo();
        BurnhopeDescriptorWriter(*renderSystem.getRenderSystemLayout(), *globalPool)
            .writeBuffer(0, &objInfo)
            .writeBuffer(1, &matInfo)
            .build(storageSet);

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
        
        if (subMeshInfos.empty()) subMeshInfos.push_back(SubMeshGPUInfo{});
        
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
        glm::mat4 prevViewProj = glm::mat4(1.0f);
        float timeAccumulator = 0.0f;
        static std::array<glm::mat4, 4> cachedCascadeMats;
        static bool matricesCached = false;
        const double targetFPS = 60.0;
        const double maxPeriod = 1.0 / targetFPS;

        while (!lveWindow.shouldClose())
        {
            glfwPollEvents();

            uiManager->ProcessPendingActions();

            uint32_t currentSubMeshCount = 0;
            registry.view<MeshComponent>().each([&](const MeshComponent &meshComp)
                                                {
                if (meshComp.model && meshComp.isVisible) {
                    currentSubMeshCount += meshComp.model->getSubMeshes().size();
                } });

            if (currentSubMeshCount != totalSubMeshCount || uiManager->GetContext().needsRebuild)
            {
                vkDeviceWaitIdle(lveDevice.device());
                if (cullingSystem)
                    cullingSystem.reset();

                if (shadowObjectSet != VK_NULL_HANDLE)
                {
                    std::vector<VkDescriptorSet> toFree = {shadowObjectSet};
                    globalPool->freeDescriptors(toFree);
                    shadowObjectSet = VK_NULL_HANDLE;
                }

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

                if (shadowObjectSet != VK_NULL_HANDLE)
                {
                    std::vector<VkDescriptorSet> toFree = {shadowObjectSet};
                    globalPool->freeDescriptors(toFree);
                }
                if (objectBuffer)
                {
                    auto objInfo = objectBuffer->descriptorInfo();
                    BurnhopeDescriptorWriter(*shadowObjectLayoutPtr, *globalPool).writeBuffer(0, &objInfo).build(shadowObjectSet);
                }
                uiManager->GetContext().needsRebuild = false;
            }

            bool transformsChanged = false;
            std::vector<glm::vec3> movedPositions;

            // AABB всех сдвинутых объектов для локального обновления теней
            glm::vec3 dirtyMin(std::numeric_limits<float>::max());
            glm::vec3 dirtyMax(std::numeric_limits<float>::lowest());
            bool hasDirtyRegion = false;

            registry.view<TransformComponent>().each([&](entt::entity entity, TransformComponent &tComp)
                                                     {
                if (tComp.transform.updatematrix) {
                    tComp.transform.updateMatrixIfNeeded();
                    transformsChanged = true;
                    movedPositions.push_back(tComp.transform.position); // Запоминаем где сдвинулись объекты

                    // Расширяем "грязную зону" с запасом радиуса объекта (например 15.0f)
                    dirtyMin = glm::min(dirtyMin, tComp.transform.position - glm::vec3(15.0f));
                    dirtyMax = glm::max(dirtyMax, tComp.transform.position + glm::vec3(15.0f));
                    hasDirtyRegion = true;

                    if (registry.any_of<ReflectionProbeComponent>(entity)) {
                        registry.get<ReflectionProbeComponent>(entity).updateNeeded = true;
                    }
                } });

            if (transformsChanged)
            {
                registry.view<LightComponent, TransformComponent>().each([&](auto entity, LightComponent &lightComp, TransformComponent &lightTrans)
                                                                         {
                    if (!lightComp.needsShadowUpdate) {
                        // Проверяем, двигался ли какой-то объект в радиусе действия этого источника света
                        for (const auto& pos : movedPositions) {
                            // + 15.0f берем как примерный максимальный размер (bounding sphere) сдвинувшегося объекта
                            if (glm::distance(pos, lightTrans.transform.position) <= lightComp.light.radius + 15.0f) {
                                lightComp.needsShadowUpdate = true;
                                break;
                            }
                        }
                    } });
            }

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
                if (globalRTReflectionSystem)
                {
                    globalRTReflectionSystem->updateDescriptors(newExtent, defaultWhiteTex);
                }
                if (globalVolumetricSystem) {
                    globalVolumetricSystem->updateDescriptors(newExtent);
                    if (globalVolumetricReadSet != VK_NULL_HANDLE) { std::vector<VkDescriptorSet> toFree = {globalVolumetricReadSet}; globalPool->freeDescriptors(toFree); }
                    VkDescriptorImageInfo volReadInfo{}; volReadInfo.imageView = globalVolumetricSystem->volumetricTex->getImageView();
                    volReadInfo.sampler = globalVolumetricSystem->volumetricTex->getSampler(); volReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    BurnhopeDescriptorWriter(*globalVolumetricReadLayout, *globalPool)
                        .writeImage(0, &volReadInfo)
                        .build(globalVolumetricReadSet);
                }
                continue;
            }
            const auto &rs = uiManager->GetContext().renderSettings;
            if (rcSystem && rcSystem->needsRebuild(rs.rcProbeGridX, rs.rcProbeGridY, rs.rcProbeGridZ, rs.rcOctaSize, rs.rcBaseRayLength)) {
                vkDeviceWaitIdle(lveDevice.device());
                rcSystem->updateConfig(rs.rcProbeGridX, rs.rcProbeGridY, rs.rcProbeGridZ, rs.rcOctaSize, rs.rcBaseRayLength);
                rcSystem->rebuildOnResize(extent, hdrOutputTexture->getImageView(), hdrOutputTexture->getSampler());
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
                auto &ctx = uiManager->GetContext();
                for (auto it = ctx.pendingDeletions.begin(); it != ctx.pendingDeletions.end();)
                {
                    if (--it->framesRemaining <= 0)
                    {
                        it = ctx.pendingDeletions.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
                if (!ctx.safeDeleteQueue.empty())
                {
                    ctx.pendingDeletions.push_back({ctx.safeDeleteQueue, 3});
                    ctx.safeDeleteQueue.clear();
                }

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
                ubo.camPos = camera.Position;
                ubo.zNear = 0.1f;
                ubo.zFar = 1000.0f;
                ubo.screenSize = glm::vec4(extent.width, extent.height, 0.f, 0.f);
                ubo.sunDir = shadowSystem->getSunDir();

                ubo.gridDimX = 16;
                ubo.gridDimY = 9;
                ubo.gridDimZ = 24;

                ubo.sunColor = glm::vec3(1.0f, 0.95f, 0.8f);
                ubo.sunIntensity = 5.0f;

                auto lightView = registry.view<LightComponent>();
                ubo.portalID = 0; // Main scene is portal 0
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
                const auto &rs = uiManager->GetContext().renderSettings;

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
                ubo.ppVignetteGrain = glm::vec4(rs.enableVignette ? rs.vignetteIntensity : 0.0f, rs.enableFilmGrain ? rs.grainIntensity : 0.0f, rs.refractionSpeed * timeAccumulator, rs.enableChromaticAberration ? rs.caIntensity : 0.0f);
                ubo.ppMotionBlur = glm::vec4(rs.enableMotionBlur ? 1.0f : 0.0f, rs.mbStrength, timeAccumulator, 0.0f);
                ubo.ppLensFlare = glm::vec4(rs.enableLensFlares ? 1.0f : 0.0f, rs.flareIntensity, rs.ghostDispersal, (float)rs.ghosts);
                ubo.ppTAA_CAS = glm::vec4(rs.enableTAA ? 1.0f : 0.0f, rs.taaBlendFactor, rs.enableCAS ? 1.0f : 0.0f, rs.casSharpness);
                ubo.ppLensAdvanced = glm::vec4(rs.flareHaloWidth, rs.flareChromaticDir, rs.autoFocus ? 1.0f : 0.0f, (float)rs.tonemapper);
                ubo.ppDistortionDirt = glm::vec4(rs.enableLensDistortion ? 1.0f : 0.0f, rs.lensDistortionStrength, rs.enableLensDirt ? 1.0f : 0.0f, rs.lensDirtIntensity);
                ubo.ppDitherAniso = glm::vec4(rs.enableDithering ? 1.0f : 0.0f, rs.ditherStrength, rs.enableScreenRefraction ? 1.0f : 0.0f, rs.refractionStrength);
                ubo.cgShadows = glm::vec4(rs.cgShadows[0], rs.cgShadows[1], rs.cgShadows[2], 1.0f);
                ubo.cgMidtones = glm::vec4(rs.cgMidtones[0], rs.cgMidtones[1], rs.cgMidtones[2], 1.0f);
                ubo.cgHighlights = glm::vec4(rs.cgHighlights[0], rs.cgHighlights[1], rs.cgHighlights[2], 1.0f);
                ubo.ppRetroParams = glm::vec4(rs.enableRetroCRT ? 1.0f : 0.0f, rs.crtScanlines, rs.glitchIntensity, rs.vhsNoise);
                ubo.ppRetroParams2 = glm::vec4((float)rs.pixelation, rs.enableVertexJitter ? rs.vertexJitterResolution : 0.0f, 0.0f, 0.0f);
                
                ubo.ppStylizedParams = glm::vec4(rs.enablePosterization ? rs.posterizationLevels : 0.0f, rs.enableKuwahara ? (float)rs.kuwaharaRadius : 0.0f, rs.enableCelShading ? rs.celShadingLevels : 0.0f, 0.0f);
                
                ubo.ppOutlineParams = glm::vec4(rs.enableOutline ? 1.0f : 0.0f, rs.outlineThickness, rs.outlineThresholdDepth, rs.outlineThresholdNormal);
                ubo.ppOutlineColor = glm::vec4(rs.outlineColor[0], rs.outlineColor[1], rs.outlineColor[2], (float)rs.outlineMode);
                ubo.ppOutlineJitter = glm::vec4(rs.enableOutlineJitter ? 1.0f : 0.0f, rs.outlineJitterSpeed, rs.outlineJitterStrength, 0.0f);
                ubo.ppWeatherSSR = glm::vec4(rs.enableWeather ? 1.0f : 0.0f, rs.weatherIntensity, rs.enableSSR ? 1.0f : 0.0f, rs.ssrSteps);
                ubo.ppSSSS = glm::vec4(rs.enableSSSS ? 1.0f : 0.0f, rs.ssssWidth, rs.ssrThickness, (float)rs.vrsMode);
                ubo.ppWeatherParams = glm::vec4(rs.weatherSpeed, rs.weatherSize, rs.weatherDensity, rs.weatherDistortion);

                // Сохраняем чистую проекцию ДО тряски для следующего кадра
                glm::mat4 unjitteredProj = ubo.projection;

                // Halton Jitter for TAA
                if (rs.enableTAA) {
                    float halton2[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
                    float halton3[8] = {0.333f, 0.666f, 0.111f, 0.444f, 0.777f, 0.222f, 0.555f, 0.888f};
                    float jitterX = (halton2[frameCount % 8] - 0.5f) / (float)extent.width;
                    float jitterY = (halton3[frameCount % 8] - 0.5f) / (float)extent.height;
                    ubo.projection[2][0] += jitterX * 2.0f;
                    ubo.projection[2][1] += jitterY * 2.0f;
                }

                ubo.invViewProj = glm::inverse(ubo.projection * ubo.view);


                static glm::vec3 prevCamPos = glm::vec3(0.0f);
                static glm::mat4 prevView = glm::mat4(1.0f);
                static glm::vec3 prevSunDir = glm::vec3(0.0f);
                static glm::vec3 prevCamDir = glm::vec3(0.0f);
                static bool firstFrame = true;

                bool csmNeedsFullUpdate = false;
                // CSM жестко привязан к камере. Если камера двигается или вращается - обязательно обновляем тени!
                if (firstFrame || prevSunDir != shadowSystem->getSunDir() || glm::distance(prevCamPos, camera.Position) > 0.05f || glm::distance(prevCamDir, camera.Orientation) > 0.005f)
                {
                    csmNeedsFullUpdate = true;
                    firstFrame = false;
                    prevCamPos = camera.Position;
                    prevCamDir = camera.Orientation;
                }
                prevView = ubo.view;
                prevSunDir = shadowSystem->getSunDir();


                                auto currentCascadeMats = shadowSystem->getCSM()->calculateMatrices(
                    camera, shadowSystem->getSunDir(),
                    {shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]});

                // 2. Если мы обновляем карту теней (камера улетела далеко или солнце сдвинулось),
                // ТОЛЬКО ТОГДА мы обновляем наш кэш матриц!
                if (csmNeedsFullUpdate || !matricesCached)
                {
                    for (int i = 0; i < 4; i++)
                    {
                        cachedCascadeMats[i] = currentCascadeMats[i];
                    }
                    matricesCached = true;
                }

                for (int i = 0; i < 4; i++)
                {
                    ubo.sunLightSpaceMatrices[i] = cachedCascadeMats[i];
                }
                ubo.cascadeSplits = glm::vec4(shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]);
                uboBuffers[frameIndex]->writeToBuffer(&ubo);
                uboBuffers[frameIndex]->flush();
                
                if (globalDecalBuffer) {
                    DecalBlock db{}; db.decalCount = 0;
                    registry.view<TransformComponent, DecalComponent>().each([&](auto entity, auto& trans, auto& decal) {
                        if (db.decalCount < 1000) {
                            trans.transform.updateMatrixIfNeeded();
                            db.decals[db.decalCount].invModelMatrix = glm::inverse(trans.transform.matrix);
                            db.decals[db.decalCount].params = glm::vec4((float)decal.albedoTexIdx, (float)decal.normalTexIdx, decal.opacity, 0.0f);
                            db.decalCount++;
                        }
                    });
                    globalDecalBuffer->writeToBuffer(&db);
                    globalDecalBuffer->flush();
                }
                
                ProbesInfo pInfo{};
                pInfo.count = 0;
                registry.view<TransformComponent, ReflectionProbeComponent>().each([&](entt::entity e, TransformComponent &t, ReflectionProbeComponent &p)
                                                                                   {
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
                    pInfo.count++; });
                if (globalRTReflectionSystem->probesBuffer)
                {
                    globalRTReflectionSystem->probesBuffer->writeToBuffer(&pInfo);
                    globalRTReflectionSystem->probesBuffer->flush();
                }

                // This buffer will be filled during the G-Buffer pass for use in the later lighting pass.
                auto* mappedPortalUbos = reinterpret_cast<GlobalUbo*>(portalUbosBuffer->getMappedMemory());


                renderPipeline.clear();
                renderPipeline.addPass("Shadow Maps Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    for (int i = 0; i < BurnhopeCSM::CASCADE_COUNT; i++) {
                        VkRenderPassBeginInfo rpInfo{};
                        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; rpInfo.renderPass = shadowSystem->getCSM()->getRenderPass();
                        rpInfo.framebuffer = shadowSystem->getCSM()->getFramebuffer(i);
                        rpInfo.renderArea.extent = { BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE };
                        VkClearValue clearVal{}; clearVal.depthStencil = { 1.0f, 0 };
                        rpInfo.clearValueCount = 1; rpInfo.pClearValues = &clearVal;
                        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
                        
                        VkClearAttachment clearAttachment{};
                        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                        clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

                        if (csmNeedsFullUpdate) {
                            VkClearRect clearRect{};
                            clearRect.rect.offset = { 0, 0 };
                            clearRect.rect.extent = { BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE };
                            clearRect.baseArrayLayer = 0;
                            clearRect.layerCount = 1;
                            vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                            VkViewport vp{}; vp.width = vp.height = (float)BurnhopeCSM::SHADOW_MAP_SIZE; vp.maxDepth = 1.0f;
                            vkCmdSetViewport(cmd, 0, 1, &vp);
                            VkRect2D sc{ {0,0}, {BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE} };
                            vkCmdSetScissor(cmd, 0, 1, &sc);
                            
                            shadowRenderSystem->renderShadow(cmd, cachedCascadeMats[i], *cullingSystem, registry, shadowObjectSet);
                        } else if (hasDirtyRegion) {
                            // Локальное кэширование: Очищаем и рисуем только в том месте, где объекты двигались!
                            glm::vec3 corners[8] = {
                                {dirtyMin.x, dirtyMin.y, dirtyMin.z}, {dirtyMax.x, dirtyMin.y, dirtyMin.z},
                                {dirtyMin.x, dirtyMax.y, dirtyMin.z}, {dirtyMax.x, dirtyMax.y, dirtyMin.z},
                                {dirtyMin.x, dirtyMin.y, dirtyMax.z}, {dirtyMax.x, dirtyMin.y, dirtyMax.z},
                                {dirtyMin.x, dirtyMax.y, dirtyMax.z}, {dirtyMax.x, dirtyMax.y, dirtyMax.z}
                            };
                            float minX = 1.0f, minY = 1.0f, maxX = 0.0f, maxY = 0.0f;
                            for(int k=0; k<8; k++) {
                                glm::vec4 pt = cachedCascadeMats[i] * glm::vec4(corners[k], 1.0f);
                                glm::vec3 ndc = glm::vec3(pt) / pt.w;
                                glm::vec2 uv = glm::vec2(ndc) * 0.5f + 0.5f;
                                minX = std::min(minX, uv.x); minY = std::min(minY, uv.y);
                                maxX = std::max(maxX, uv.x); maxY = std::max(maxY, uv.y);
                            }
                            int pxX = std::max(0, (int)(minX * BurnhopeCSM::SHADOW_MAP_SIZE) - 4);
                            int pxY = std::max(0, (int)(minY * BurnhopeCSM::SHADOW_MAP_SIZE) - 4);
                            int pW = std::min((int)BurnhopeCSM::SHADOW_MAP_SIZE, (int)(maxX * BurnhopeCSM::SHADOW_MAP_SIZE) + 4) - pxX;
                            int pH = std::min((int)BurnhopeCSM::SHADOW_MAP_SIZE, (int)(maxY * BurnhopeCSM::SHADOW_MAP_SIZE) + 4) - pxY;

                            if (pW > 0 && pH > 0 && pxX < BurnhopeCSM::SHADOW_MAP_SIZE && pxY < BurnhopeCSM::SHADOW_MAP_SIZE) {
                                VkClearRect clearRect{};
                                clearRect.rect.offset = { pxX, pxY };
                                clearRect.rect.extent = { (uint32_t)pW, (uint32_t)pH };
                                clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                                vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                                VkViewport vp{}; vp.width = vp.height = (float)BurnhopeCSM::SHADOW_MAP_SIZE; vp.maxDepth = 1.0f;
                                vkCmdSetViewport(cmd, 0, 1, &vp);
                                VkRect2D sc{ {pxX, pxY}, {(uint32_t)pW, (uint32_t)pH} };
                                vkCmdSetScissor(cmd, 0, 1, &sc);

                               shadowRenderSystem->renderShadow(cmd, cachedCascadeMats[i], *cullingSystem, registry, shadowObjectSet);
                            }
                        }
                        vkCmdEndRenderPass(cmd);
                    }
                    VkRenderPassBeginInfo clearPass{};
                    clearPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; clearPass.renderPass = shadowSystem->getAtlas()->getRenderPass();
                    clearPass.framebuffer = shadowSystem->getAtlas()->getFramebuffer();
                    clearPass.renderArea.extent = { BurnhopeShadowAtlas::ATLAS_RESOLUTION, BurnhopeShadowAtlas::ATLAS_RESOLUTION };
                    
                    // Возвращаем dummy clear value чтобы Vulkan Validation Layers не крашили приложение. 
                    // Но чтобы кэш работал, в shadow.cpp обязательно нужно поменять VK_ATTACHMENT_LOAD_OP_CLEAR на VK_ATTACHMENT_LOAD_OP_LOAD.
                    VkClearValue dummyClear{}; dummyClear.depthStencil = { 1.0f, 0 };
                    clearPass.clearValueCount = 1; clearPass.pClearValues = &dummyClear;
                    
                    vkCmdBeginRenderPass(cmd, &clearPass, VK_SUBPASS_CONTENTS_INLINE);
                    auto lightView = registry.view<LightComponent, TransformComponent>();
                    for (auto entity : lightView) {
                        auto& lightComp = lightView.get<LightComponent>(entity);
                        auto& light = lightComp.light;
                        auto& trans = lightView.get<TransformComponent>(entity).transform;
                        
                        if (!light.enable || !light.castShadows || light.type == LightType::Directional || light.shadowSlot < 0 || !lightComp.needsShadowUpdate) continue;

                        VkClearAttachment clearAttachment{};
                        clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                        clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

                        int tileSize = light.shadowTileSize;
                        int pxX = (light.shadowSlot % BurnhopeShadowAtlas::ATLAS_IN_UNITS) * BurnhopeShadowAtlas::MIN_TILE;
                        int pxY = (light.shadowSlot / BurnhopeShadowAtlas::ATLAS_IN_UNITS) * BurnhopeShadowAtlas::MIN_TILE;
                        if (light.type == LightType::Spot) {
                            shadowSystem->getAtlas()->setTileViewport(cmd, pxX, pxY, tileSize);
                            
                            VkClearRect clearRect{};
                            clearRect.rect.offset = { pxX, pxY };
                            clearRect.rect.extent = { (uint32_t)tileSize, (uint32_t)tileSize };
                            clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                            vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

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

                                VkClearRect clearRect{};
                                clearRect.rect.offset = { fx, fy };
                                clearRect.rect.extent = { (uint32_t)tileSize, (uint32_t)tileSize };
                                clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                                vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                                shadowRenderSystem->renderShadow(cmd, faceMatrix, *cullingSystem, registry, shadowObjectSet);
                            }
                        }
                        lightComp.needsShadowUpdate = false; // Кэшируем до следующих изменений
                    }
                    vkCmdEndRenderPass(cmd); });
                renderPipeline.addPass("Frustum Culling", [&](VkCommandBuffer cmd)
                                       {
                    auto vp = ubo.projection * ubo.view;
                    auto planes = CullingSystem::extractFrustumPlanes(vp);
                    cullingSystem->dispatchCulling(cmd, vp, ubo.camPos, planes, totalSubMeshCount); });

                renderPipeline.addPass("Light Culling Pass", {}, [&](VkCommandBuffer cmd)
                                       {
                    // Очищаем счетчик globalIndexCount в начале кадра
                    vkCmdFillBuffer(cmd, dummyIndexBuffer->getBuffer(), 0, 4, 0);
                    
                    VkBufferMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                    barrier.buffer = dummyIndexBuffer->getBuffer();
                    barrier.size = 4;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &barrier, 0, nullptr);

                    lightCullingShader->bind(cmd);
                    lightCullingShader->bindDescriptorSets(cmd, { globalDescriptorSets[frameIndex], lightSet });
                    lightCullingShader->dispatch(cmd, (16 + 7) / 8, (9 + 7) / 8, (24 + 7) / 8); 

                    // Ожидаем завершения кластеризации перед освещением
                    VkBufferMemoryBarrier gridBarriers[2];
                    gridBarriers[0] = barrier; gridBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; gridBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT; gridBarriers[0].buffer = dummyGridBuffer->getBuffer(); gridBarriers[0].size = VK_WHOLE_SIZE;
                    gridBarriers[1] = barrier; gridBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; gridBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT; gridBarriers[1].buffer = dummyIndexBuffer->getBuffer(); gridBarriers[1].size = VK_WHOLE_SIZE;

                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 2, gridBarriers, 0, nullptr); });

                renderPipeline.addPass("G-Buffer Pass", {RenderPipeline::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT| VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT), RenderPipeline::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    VkRenderPassBeginInfo renderPassInfo{};
                    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; renderPassInfo.renderPass = gBuffer->getRenderPass();
                    renderPassInfo.framebuffer = gBuffer->getFramebuffer(); renderPassInfo.renderArea.extent = extent; 
                    std::array<VkClearValue, 6> clearValues{}; clearValues[4].color.uint32[0] = 0; clearValues[5].depthStencil = { 1.0f, 0 };
                    renderPassInfo.clearValueCount = (uint32_t)clearValues.size(); renderPassInfo.pClearValues = clearValues.data();
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
                            portalUbo.portalID    = portalCounter + 1;
                        
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

                            // Also write to the big SSBO for the compute lighting pass
                            if(portalCounter < MAX_PORTALS) mappedPortalUbos[portalCounter] = portalUbo;
                        
                            FrameInfo portalFrameInfo  = frameInfo;
                            portalFrameInfo.globalDescriptorSet = portalDescriptorSets[portalCounter][frameIndex];
                        
                            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, portalCounter + 1);
                            simpleRenderSystem->renderEntities(portalFrameInfo, registry, storageSet, textureSet,
                                                               *cullingSystem, totalSubMeshCount, true);
                            portalCounter++;
                        }
                    }

                    vkCmdEndRenderPass(cmd); });

                renderPipeline.addPass("VSM Mark Pages", {}, [&](VkCommandBuffer cmd)
                                       {
                    if (csmNeedsFullUpdate) {
                        // Очистка таблиц перед анализом только при полном обновлении
                        vkCmdFillBuffer(cmd, shadowSystem->getVSM()->getPageTable()->getBuffer(), 0, VK_WHOLE_SIZE, 0xFFFFFFFF);
                        vkCmdFillBuffer(cmd, shadowSystem->getVSM()->getAllocator()->getBuffer(), 0, 4, 0); // Обнуляем счетчик

                        VkBufferMemoryBarrier barriers[2]{};
                        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barriers[0].dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT; // Маркировщик будет сюда только писать
                        barriers[0].buffer = shadowSystem->getVSM()->getPageTable()->getBuffer();
                        barriers[0].size = VK_WHOLE_SIZE;

                        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                        barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
                        barriers[1].buffer = shadowSystem->getVSM()->getAllocator()->getBuffer();
                        barriers[1].size = VK_WHOLE_SIZE;

                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 2, barriers, 0, nullptr);
                    }

                    vsmMarkPagesShader->bind(cmd);
                    vsmMarkPagesShader->bindDescriptorSets(cmd, { globalDescriptorSets[frameIndex], gBufferSet, vsmSet, lightSet });
                    
                    // Вызываем шейдер по количеству источников света (хватит 4 групп по 32 потока = 128)
                    vsmMarkPagesShader->dispatch(cmd, 4, 1, 1); 
                    
                    // КРИТИЧЕСКИ ВАЖНО: Барьер, чтобы Compute Lighting увидел записанные страницы!
                    VkBufferMemoryBarrier vsmBarrier{};
                    vsmBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                    vsmBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
                    vsmBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vsmBarrier.buffer = shadowSystem->getVSM()->getPageTable()->getBuffer();
                    vsmBarrier.size = VK_WHOLE_SIZE;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &vsmBarrier, 0, nullptr); });

                renderPipeline.addPass("VSM Geometry Render", {RenderPipeline::createImageBarrier(shadowSystem->getVSM()->getPhysicalAtlas()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    VkRenderPassBeginInfo rpInfo{};
                    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO; 
                    rpInfo.renderPass = shadowSystem->getVSM()->getRenderPass();
                    rpInfo.framebuffer = shadowSystem->getVSM()->getFramebuffer();
                    rpInfo.renderArea.extent = { 4096, 4096 };
                    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

                    VkClearAttachment clearAttachment{};
                    clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

                    if (csmNeedsFullUpdate) {
                        VkClearRect clearRect{};
                        clearRect.rect.offset = { 0, 0 };
                        clearRect.rect.extent = { 4096, 4096 };
                        clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                        vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                        auto lightView = registry.view<LightComponent, TransformComponent>();
                        for (auto entity : lightView) {
                            auto& light = lightView.get<LightComponent>(entity).light;
                            auto& trans = lightView.get<TransformComponent>(entity).transform;
                            if (light.enable && light.castShadows && light.shadowSlot >= 0 && (light.type == LightType::Spot || light.type == LightType::Point)) {
                                int encodedInt = light.shadowSlot;
                                int realSlot = encodedInt / 10000;
                                int tileSize = encodedInt % 10000;
                                int pxX = (realSlot % 32) * 128;
                                int pxY = (realSlot / 32) * 128;

                                if (light.type == LightType::Spot) {
                                    VkViewport vp{}; vp.x = pxX; vp.y = pxY; vp.width = tileSize; vp.height = tileSize; vp.maxDepth = 1.0f;
                                    vkCmdSetViewport(cmd, 0, 1, &vp);
                                    VkRect2D sc{ {pxX, pxY}, {(uint32_t)tileSize, (uint32_t)tileSize} };
                                    vkCmdSetScissor(cmd, 0, 1, &sc);
                                    shadowRenderSystem->renderShadow(cmd, light.lightSpaceMatrix, *cullingSystem, registry, shadowObjectSet);
                                } else if (light.type == LightType::Point) {
                                    glm::vec3 pos = trans.position;
                                    const glm::vec3 dirs[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
                                    const glm::vec3 ups[6]  = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};  
                                    for (int face = 0; face < 6; face++) {
                                        int fx = pxX + face * tileSize; int fy = pxY;
                                        if (fx + tileSize > 4096) { fx = fx % 4096; fy += tileSize; }
                                        VkViewport vp{}; vp.x = fx; vp.y = fy; vp.width = tileSize; vp.height = tileSize; vp.maxDepth = 1.0f;
                                        vkCmdSetViewport(cmd, 0, 1, &vp);
                                        VkRect2D sc{ {fx, fy}, {(uint32_t)tileSize, (uint32_t)tileSize} };
                                        vkCmdSetScissor(cmd, 0, 1, &sc);
                                        // ФИКС: Используем shadowPerspective для переворота Y-оси!
                                        glm::mat4 faceProj = shadowPerspective(90.0f, 1.0f, 0.1f, light.radius);
                                        glm::mat4 faceView = glm::lookAt(pos, pos + dirs[face], ups[face]);
                                        glm::mat4 faceMatrix = faceProj * faceView;
                                        shadowRenderSystem->renderShadow(cmd, faceMatrix, *cullingSystem, registry, shadowObjectSet);
                                    }
                                }
                            }
                        }
                    } else if (hasDirtyRegion) {
                        auto lightView = registry.view<LightComponent, TransformComponent>();
                        for (auto entity : lightView) {
                            auto& light = lightView.get<LightComponent>(entity).light;
                            auto& trans = lightView.get<TransformComponent>(entity).transform;
                            if (light.enable && light.castShadows && light.shadowSlot >= 0 && (light.type == LightType::Spot || light.type == LightType::Point)) {
                                int encodedInt = light.shadowSlot;
                                int realSlot = encodedInt / 10000;
                                int tileSize = encodedInt % 10000;
                                int pxX = (realSlot % 32) * 128;
                                int pxY = (realSlot / 32) * 128;

                                glm::mat4 lightMatrix = light.lightSpaceMatrix;
                                glm::vec3 corners[8] = {
                                    {dirtyMin.x, dirtyMin.y, dirtyMin.z}, {dirtyMax.x, dirtyMin.y, dirtyMin.z},
                                    {dirtyMin.x, dirtyMax.y, dirtyMin.z}, {dirtyMax.x, dirtyMax.y, dirtyMin.z},
                                    {dirtyMin.x, dirtyMin.y, dirtyMax.z}, {dirtyMax.x, dirtyMin.y, dirtyMax.z},
                                    {dirtyMin.x, dirtyMax.y, dirtyMax.z}, {dirtyMax.x, dirtyMax.y, dirtyMax.z}
                                };
                                float minX = 1.0f, minY = 1.0f, maxX = 0.0f, maxY = 0.0f;
                                for(int k=0; k<8; k++) {
                                    glm::vec4 pt = lightMatrix * glm::vec4(corners[k], 1.0f);
                                    glm::vec3 ndc = glm::vec3(pt) / pt.w;
                                    glm::vec2 uv = glm::vec2(ndc) * 0.5f + 0.5f;
                                    minX = std::min(minX, uv.x); minY = std::min(minY, uv.y);
                                    maxX = std::max(maxX, uv.x); maxY = std::max(maxY, uv.y);
                                }
                                int localPxX = std::max(0, (int)(minX * tileSize) - 5);
                                int localPxY = std::max(0, (int)(minY * tileSize) - 5);
                                int pW = std::min(tileSize, (int)(maxX * tileSize) + 5) - localPxX;
                                int pH = std::min(tileSize, (int)(maxY * tileSize) + 5) - localPxY;

                                if (pW > 0 && pH > 0) {
                                    VkClearRect clearRect{};
                                    clearRect.rect.offset = { pxX + localPxX, pxY + localPxY };
                                    clearRect.rect.extent = { (uint32_t)pW, (uint32_t)pH };
                                    clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                                    vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                                    VkViewport vp{}; vp.x = pxX; vp.y = pxY; vp.width = tileSize; vp.height = tileSize; vp.maxDepth = 1.0f;
                                    vkCmdSetViewport(cmd, 0, 1, &vp);
                                    VkRect2D sc{ {pxX + localPxX, pxY + localPxY}, {(uint32_t)pW, (uint32_t)pH} };
                                    vkCmdSetScissor(cmd, 0, 1, &sc);
                                    if (light.type == LightType::Spot) {
                                        shadowRenderSystem->renderShadow(cmd, light.lightSpaceMatrix, *cullingSystem, registry, shadowObjectSet);
                                    } else if (light.type == LightType::Point) {
                                        glm::vec3 pos = trans.position;
                                        const glm::vec3 dirs[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
                                        const glm::vec3 ups[6]  = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};  
                                        for (int face = 0; face < 6; face++) {
                                            int fx = pxX + face * tileSize; int fy = pxY;
                                            if (fx + tileSize > 4096) { fx = fx % 4096; fy += tileSize; }
                                            vp.x = fx; vp.y = fy; vkCmdSetViewport(cmd, 0, 1, &vp);
                                            sc.offset = { fx + localPxX, fy + localPxY }; vkCmdSetScissor(cmd, 0, 1, &sc);
                                            // ФИКС: Используем shadowPerspective!
                                            glm::mat4 faceProj = shadowPerspective(90.0f, 1.0f, 0.1f, light.radius);
                                            glm::mat4 faceView = glm::lookAt(pos, pos + dirs[face], ups[face]);
                                            glm::mat4 faceMatrix = faceProj * faceView;
                                            shadowRenderSystem->renderShadow(cmd, faceMatrix, *cullingSystem, registry, shadowObjectSet);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    vkCmdEndRenderPass(cmd); });

                renderPipeline.addPass("Hi-Z Pass", {RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)}, [&](VkCommandBuffer cmd)
                                       { hizSystem->compute(cmd, extent); });
                renderPipeline.addPass("GTAO Pass", {RenderPipeline::createImageBarrier(gtaoOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       { gtaoSystem->compute(cmd, globalDescriptorSets[frameIndex], gtaoSet, std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)); });
                renderPipeline.addPass("Radiance Cascades GI", {RenderPipeline::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)},
                                       [&](VkCommandBuffer cmd)
                                       {
                    glm::vec3 sceneMin(-8.0f, -8.0f, -8.0f);
                    glm::vec3 sceneMax( 8.0f,  8.0f,  8.0f);
                    rcSystem->dispatch(cmd, globalDescriptorSets[frameIndex], gBufferSet, ubo.invViewProj, camera.Position, sceneMin, sceneMax, extent, rtSet, storageSet, textureSet); });

                renderPipeline.addPass("Probe Update", {}, [&](VkCommandBuffer cmd)
                                       { registry.view<TransformComponent, ReflectionProbeComponent>().each([&](entt::entity e, TransformComponent &t, ReflectionProbeComponent &p)
                                                                                                            {
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
                        } }); });

                renderPipeline.addPass("RT Reflections", {RenderPipeline::createImageBarrier(globalRTReflectionSystem->rtReflectionsTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
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
                    globalRTReflectionSystem->rtReflectionsShader->dispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1); });
                    
                renderPipeline.addPass("Volumetric Fog Pass", {
                    RenderPipeline::createImageBarrier(globalVolumetricSystem->volumetricTex->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)
                }, [&](VkCommandBuffer cmd) {
                    globalVolumetricSystem->shader->bind(cmd);
                    globalVolumetricSystem->shader->bindDescriptorSets(cmd, { globalDescriptorSets[frameIndex], gBufferSet, shadowSet, lightSet, vsmSet, globalVolumetricSystem->writeSet, portalInfoSet });
                    uint32_t vw = std::max(1u, extent.width / 2);
                    uint32_t vh = std::max(1u, extent.height / 2);
                    globalVolumetricSystem->shader->dispatch(cmd, (vw + 15)/16, (vh + 15)/16, 1);
                });

                renderPipeline.addPass("Compute Lighting", {
                    RenderPipeline::createImageBarrier(shadowSystem->getVSM()->getPhysicalAtlas()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1), 
                    RenderPipeline::createImageBarrier(globalVolumetricSystem->volumetricTex->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                    RenderPipeline::createImageBarrier(gtaoOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
                }, [&](VkCommandBuffer cmd)
                                       {
                    std::vector<VkDescriptorSet> computeSets = { globalDescriptorSets[frameIndex], gBufferSet, shadowSet, lightSet, computeOutputSet, rcSystem->getGISet(), gtaoSet, rtSet, globalRTReflectionSystem->rtSet, vsmSet, portalInfoSet, globalVolumetricReadSet };
                    lightingSystem->computeLighting(cmd, computeSets, extent.width, extent.height); });

                renderPipeline.addPass("SSGI Pass", {RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       { ssgiSystem->computeSSGI(cmd, globalDescriptorSets[frameIndex], gBufferSet, shadowSet, ssgiSet, extent.width, extent.height); });
                renderPipeline.addPass("SSGI Denoise Pass", {RenderPipeline::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)}, [&](VkCommandBuffer cmd)
                                       { ssgiSystem->computeDenoise(cmd, globalDescriptorSets[frameIndex], gBufferSet, ssgiSet, extent.width, extent.height); });
                renderPipeline.addPass("Post Processing Pass", {RenderPipeline::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
            postProcessShader->bind(cmd);
            postProcessShader->bindDescriptorSets(cmd, { globalDescriptorSets[frameIndex], postProcessSet });
            postProcessShader->dispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1); });
                
                renderPipeline.addPass("TAA History Update", {RenderPipeline::createImageBarrier(taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT), RenderPipeline::createImageBarrier(taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    VkImageBlit blit{};
                    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.dstOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    vkCmdBlitImage(cmd, taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
                });

                VkImage swapChainImage = lveRenderer.getCurrentSwapChainImage();
                renderPipeline.addPass("Blit and UI", {RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT), RenderPipeline::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)}, [&](VkCommandBuffer cmd)
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
                renderPipeline.addPass("Reset Layouts", {RenderPipeline::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT), RenderPipeline::createImageBarrier(taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT), RenderPipeline::createImageBarrier(taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT)}, [](VkCommandBuffer cmd) {});
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
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
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
            
        taaHistoryTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
                                                               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT);
        taaHistoryTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
            
        taaResolvedTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT);
        taaResolvedTexture->transitionLayout(
            lveDevice.beginSingleTimeCommands(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);

        gBufferLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                               .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // Emissive
                               .addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT) // PortalID
                               .build();
        outputLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                              .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                              .build();
        postProcessLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                   .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                                   .build();
        exposureBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(float), 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        exposureBuffer->map();
        float initLuma = 1.0f;
        exposureBuffer->writeToBuffer(&initLuma);
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
                              .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                             .build();
        
        globalDecalBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(DecalBlock), 1,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        globalDecalBuffer->map();

        vsmLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                           .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT) // Физ. Атлас
                           .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)         // Page Table
                           .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)         // Allocator
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
        portalInfoLayoutPtr = BurnhopeDescriptorSetLayout::Builder(lveDevice)
            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
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

        VkDescriptorImageInfo portalIdInfo{};
        portalIdInfo.sampler = gBuffer->getPortalID()->getSampler();
        portalIdInfo.imageView = gBuffer->getPortalID()->getImageView();
        portalIdInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
            .writeImage(0, &normInfo)
            .writeImage(1, &albInfo)
            .writeImage(2, &extraInfo)
            .writeImage(3, &depthInfo)
            .writeImage(4, &emissiveInfo)
            .writeImage(5, &portalIdInfo)
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
        VkDescriptorImageInfo historyInfo{};
        historyInfo.sampler = taaHistoryTexture->getSampler();
        historyInfo.imageView = taaHistoryTexture->getImageView();
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo resolvedInfo{};
        resolvedInfo.imageView = taaResolvedTexture->getImageView();
        resolvedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        auto exposureInfo = exposureBuffer->descriptorInfo();

        VkDescriptorImageInfo dirtInfo{};
        dirtInfo.sampler = defaultDirtTex->getSampler();
        dirtInfo.imageView = defaultDirtTex->getImageView();
        dirtInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        BurnhopeDescriptorWriter(*postProcessLayoutPtr, *globalPool)
            .writeImage(0, &ppOutInfo)
            .writeImage(1, &ppInInfo)
            .writeImage(2, &depthInfo)
            .writeImage(3, &historyInfo)
            .writeImage(4, &resolvedInfo)
            .writeBuffer(5, &exposureInfo)
            .writeImage(6, &dirtInfo)
            .writeImage(7, &normInfo)
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
        if (vkCreateImageView(lveDevice.device(), &arrayViewInfo, nullptr, &csmArrayView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create 2D Array View for CSM!");
        }
        if (std::filesystem::exists("../textures/bluenoise.png"))
        {
            blueNoiseTex = BurnhopeTexture::createDataTextureFromFile(lveDevice, "../textures/bluenoise.png");
            std::cout << "yes";
        }
        else
        {
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
            16 * 9 * 24 * 100 + 1, // Размер: ячейки * макс свет + счетчик
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        auto gridInfo = dummyGridBuffer->descriptorInfo();
        auto indexInfo = dummyIndexBuffer->descriptorInfo();
        auto lightBufInfo = lightUboBuffer->descriptorInfo();
        auto faceMatInfo = faceMatricesBuffer->descriptorInfo();
        auto decalBufInfo = globalDecalBuffer->descriptorInfo();
        BurnhopeDescriptorWriter(*lightLayoutPtr, *globalPool)
            .writeBuffer(0, &lightBufInfo)
            .writeBuffer(1, &gridInfo)
            .writeBuffer(2, &indexInfo)
            .writeBuffer(3, &faceMatInfo)
            .writeBuffer(4, &decalBufInfo)
            .build(lightSet);

        portalUbosBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice, sizeof(GlobalUbo), MAX_PORTALS,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        portalUbosBuffer->map();

        auto portalUbosInfo = portalUbosBuffer->descriptorInfo();
        BurnhopeDescriptorWriter(*portalInfoLayoutPtr, *globalPool)
            .writeBuffer(0, &portalUbosInfo)
            .build(portalInfoSet);

        globalRTReflectionSystem = std::make_unique<RTReflectionSystem>(lveDevice, *globalPool);
        globalRTReflectionSystem->init(lveWindow.getExtent(), globalSetLayouts, gBufferLayoutPtr->getDescriptorSetLayout(), rtLayoutPtr->getDescriptorSetLayout(), simpleRenderSystem->getRenderSystemLayout()->getDescriptorSetLayout(), simpleRenderSystem->getTextureLayout()->getDescriptorSetLayout(), giLayoutPtr->getDescriptorSetLayout());
        globalRTReflectionSystem->updateDescriptors(lveWindow.getExtent(), defaultWhiteTex);
        
        globalVolumetricReadLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice).addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT).build();
        globalVolumetricSystem = std::make_unique<VolumetricSystem>(lveDevice, *globalPool);
        globalVolumetricSystem->init(lveWindow.getExtent(), globalSetLayouts, gBufferLayoutPtr->getDescriptorSetLayout(), shadowLayoutPtr->getDescriptorSetLayout(), lightLayoutPtr->getDescriptorSetLayout(), vsmLayoutPtr->getDescriptorSetLayout(), portalInfoLayoutPtr->getDescriptorSetLayout());
        globalVolumetricSystem->updateDescriptors(lveWindow.getExtent());
        
        VkDescriptorImageInfo volReadInfo{};
        volReadInfo.imageView = globalVolumetricSystem->volumetricTex->getImageView();
        volReadInfo.sampler = globalVolumetricSystem->volumetricTex->getSampler();
        volReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        BurnhopeDescriptorWriter(*globalVolumetricReadLayout, *globalPool)
            .writeImage(0, &volReadInfo)
            .build(globalVolumetricReadSet);

        std::vector<VkDescriptorSetLayout> computeLayouts = {
            globalSetLayouts,
            gBufferLayoutPtr->getDescriptorSetLayout(),
            shadowLayoutPtr->getDescriptorSetLayout(),
            lightLayoutPtr->getDescriptorSetLayout(),
            outputLayoutPtr->getDescriptorSetLayout(),
            giLayoutPtr->getDescriptorSetLayout(),
            gtaoLayoutPtr->getDescriptorSetLayout(),
            rtLayoutPtr->getDescriptorSetLayout(),
            globalRTReflectionSystem->rtLayoutPtr->getDescriptorSetLayout(),
            vsmLayoutPtr->getDescriptorSetLayout(),
            portalInfoLayoutPtr->getDescriptorSetLayout(),
            globalVolumetricReadLayout->getDescriptorSetLayout(),
            simpleRenderSystem->getTextureLayout()->getDescriptorSetLayout()};
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
            simpleRenderSystem->getTextureLayout()->getDescriptorSetLayout(),
            16, 9, 24, 8, 1.0f);

        std::vector<VkDescriptorSetLayout> ppLayouts = {globalSetLayouts, postProcessLayoutPtr->getDescriptorSetLayout()};
        postProcessShader = std::make_unique<ComputeShader>(lveDevice, "shaders/post_process.comp.spv", ppLayouts);

        std::vector<VkDescriptorSetLayout> cullLayouts = {globalSetLayouts, lightLayoutPtr->getDescriptorSetLayout()};
        lightCullingShader = std::make_unique<ComputeShader>(lveDevice, "shaders/light_culling.comp.spv", cullLayouts);

        // Инициализация VSM дескрипторов и шейдера
        auto vsmAtlasInfo = shadowSystem->getVSM()->getPhysicalAtlas()->getImageInfo();
        auto vsmPageTableInfo = shadowSystem->getVSM()->getPageTable()->descriptorInfo();
        auto vsmAllocInfo = shadowSystem->getVSM()->getAllocator()->descriptorInfo();
        BurnhopeDescriptorWriter(*vsmLayoutPtr, *globalPool)
            .writeImage(0, &vsmAtlasInfo)
            .writeBuffer(1, &vsmPageTableInfo)
            .writeBuffer(2, &vsmAllocInfo)
            .build(vsmSet);

        std::vector<VkDescriptorSetLayout> vsmMarkLayouts = {globalSetLayouts, gBufferLayoutPtr->getDescriptorSetLayout(), vsmLayoutPtr->getDescriptorSetLayout(), lightLayoutPtr->getDescriptorSetLayout()};
        vsmMarkPagesShader = std::make_unique<ComputeShader>(lveDevice, "shaders/vsm_mark_pages.comp.spv", vsmMarkLayouts);

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
            ssgiLayoutPtr->getDescriptorSetLayout());
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

        VkDescriptorImageInfo portalIdInfo{};
        portalIdInfo.sampler = gBuffer->getPortalID()->getSampler();
        portalIdInfo.imageView = gBuffer->getPortalID()->getImageView();
        portalIdInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        bool ok = BurnhopeDescriptorWriter(*gBufferLayoutPtr, *globalPool)
                      .writeImage(0, &normInfo)
                      .writeImage(1, &albInfo)
                      .writeImage(2, &extraInfo)
                      .writeImage(3, &depthInfo)
                      .writeImage(4, &emissiveInfo)
                      .writeImage(5, &portalIdInfo)
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
        VkExtent3D gtaoExtent = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2), 1};
        gtaoOutputTexture = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R8_UNORM, gtaoExtent,
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
        
        if (globalVolumetricSystem) {
            // Передаем только ширину и высоту, создавая VkExtent2D на лету
            globalVolumetricSystem->updateDescriptors({extent.width, extent.height});
            
            if (globalVolumetricReadSet != VK_NULL_HANDLE) { 
                std::vector<VkDescriptorSet> toFree = {globalVolumetricReadSet}; 
                globalPool->freeDescriptors(toFree); 
            }
            
            VkDescriptorImageInfo volReadInfo{}; 
            volReadInfo.imageView = globalVolumetricSystem->volumetricTex->getImageView();
            volReadInfo.sampler = globalVolumetricSystem->volumetricTex->getSampler(); 
            volReadInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            
            BurnhopeDescriptorWriter(*globalVolumetricReadLayout, *globalPool)
                .writeImage(0, &volReadInfo)
                .build(globalVolumetricReadSet);
        }
        if (!ok || computeOutputSet == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Failed to rebuild computeOutputSet!");
        }

        postProcessTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT);
        postProcessTexture->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        taaHistoryTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_SAMPLE_COUNT_1_BIT);
        taaHistoryTexture->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        taaResolvedTexture = std::make_unique<BurnhopeTexture>(lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_SAMPLE_COUNT_1_BIT);
        taaResolvedTexture->transitionLayout(lveDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorImageInfo ppOutInfo{};
        ppOutInfo.imageView = postProcessTexture->getImageView();
        ppOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo ppInInfo{};
        ppInInfo.sampler = hdrOutputTexture->getSampler();
        ppInInfo.imageView = hdrOutputTexture->getImageView();
        ppInInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo historyInfo{};
        historyInfo.sampler = taaHistoryTexture->getSampler();
        historyInfo.imageView = taaHistoryTexture->getImageView();
        historyInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        VkDescriptorImageInfo resolvedInfo{};
        resolvedInfo.imageView = taaResolvedTexture->getImageView();
        resolvedInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        auto exposureInfo = exposureBuffer->descriptorInfo();
        VkDescriptorImageInfo dirtInfo{};
        dirtInfo.sampler = defaultDirtTex->getSampler();
        dirtInfo.imageView = defaultDirtTex->getImageView();
        dirtInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        BurnhopeDescriptorWriter(*postProcessLayoutPtr, *globalPool)
            .writeImage(0, &ppOutInfo)
            .writeImage(1, &ppInInfo)
            .writeImage(2, &depthInfo)
            .writeImage(3, &historyInfo)
            .writeImage(4, &resolvedInfo)
            .writeBuffer(5, &exposureInfo)
            .writeImage(6, &dirtInfo)
            .writeImage(7, &normInfo)
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
        auto pfnDestroyAccelerationStructureKHR = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkDestroyAccelerationStructureKHR");

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

        uint32_t primitiveCount = static_cast<uint32_t>(instances.size());
        uint32_t bufferInstanceCount = std::max(1u, primitiveCount);
        VkDeviceSize instancesBufferSize = bufferInstanceCount * sizeof(VkAccelerationStructureInstanceKHR);

        instancesBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            sizeof(VkAccelerationStructureInstanceKHR),
            bufferInstanceCount,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (primitiveCount > 0) {
            BurnhopeBuffer stagingBuffer{
                lveDevice, sizeof(VkAccelerationStructureInstanceKHR), primitiveCount,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
            stagingBuffer.map();
            stagingBuffer.writeToBuffer((void *)instances.data());
            lveDevice.copyBuffer(stagingBuffer.getBuffer(), instancesBuffer->getBuffer(), primitiveCount * sizeof(VkAccelerationStructureInstanceKHR));
        }

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

        if (tlasHandle != VK_NULL_HANDLE && pfnDestroyAccelerationStructureKHR)
        {
            pfnDestroyAccelerationStructureKHR(lveDevice.device(), tlasHandle, nullptr);
            tlasHandle = VK_NULL_HANDLE;
        }



        VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
        sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        pfnGetAccelerationStructureBuildSizesKHR(lveDevice.device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &primitiveCount, &sizeInfo);

        VkDeviceSize tlasSize = std::max<VkDeviceSize>(256, sizeInfo.accelerationStructureSize);
        tlasBuffer = std::make_unique<BurnhopeBuffer>(
            lveDevice,
            tlasSize, 1,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkAccelerationStructureCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        createInfo.buffer = tlasBuffer->getBuffer();
        createInfo.size = tlasSize;
        createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        pfnCreateAccelerationStructureKHR(lveDevice.device(), &createInfo, nullptr, &tlasHandle);

        VkDeviceSize scratchSize = std::max<VkDeviceSize>(256, sizeInfo.buildScratchSize);
        BurnhopeBuffer scratchBuffer(
            lveDevice, scratchSize, 1,
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
        /*
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
        /*
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
        */
    }

}