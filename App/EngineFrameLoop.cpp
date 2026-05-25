#include "Engine.hpp"
#include "../Render/RenderDebug.hpp"
#include "EngineMath.hpp"
#include "Systems/GpuProbeTypes.hpp"
#include "../Render/Camera.hpp"
#include "../Render/Core/FrameGraph.hpp"
#include "../Render/Model.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include "../Utils/BindlessPush.hpp"
#include "../Utils/GpuAddress.hpp"
#include "../Utils/FrameInfo.hpp"
#include <array>
#include <chrono>
#include <iostream>
#include <thread>

namespace burnhope {

namespace {

void debugPresentSolidColor(
    VkCommandBuffer cmd,
    VkImage swapImage,
    VkClearColorValue color) {
  VkImageMemoryBarrier2 toTransfer = FrameGraph::createImageBarrier(
      swapImage,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
      0,
      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      VK_ACCESS_2_TRANSFER_WRITE_BIT);
  VkDependencyInfo depTo{};
  depTo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  depTo.imageMemoryBarrierCount = 1;
  depTo.pImageMemoryBarriers = &toTransfer;
  vkCmdPipelineBarrier2(cmd, &depTo);

  VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdClearColorImage(cmd, swapImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1, &range);

  VkImageMemoryBarrier2 toPresent = FrameGraph::createImageBarrier(
      swapImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      VK_ACCESS_2_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
      0);
  VkDependencyInfo depPresent{};
  depPresent.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  depPresent.imageMemoryBarrierCount = 1;
  depPresent.pImageMemoryBarriers = &toPresent;
  vkCmdPipelineBarrier2(cmd, &depPresent);
}

void transitionSwapchainToPresent(VkCommandBuffer cmd, VkImage swapImage) {
  VkImageMemoryBarrier2 toPresent = FrameGraph::createImageBarrier(
      swapImage,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
      VK_ACCESS_2_TRANSFER_WRITE_BIT,
      VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
      0);
  VkDependencyInfo depPresent{};
  depPresent.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  depPresent.imageMemoryBarrierCount = 1;
  depPresent.pImageMemoryBarriers = &toPresent;
  vkCmdPipelineBarrier2(cmd, &depPresent);
}

} // namespace

    void Engine::run()
    {

        // std::vector<std::unique_ptr<BurnhopeBuffer>> portalUboBuffers(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
        // for (int i = 0; i < (int)portalUboBuffers.size(); i++)
        //{
        //      portalUboBuffers[i] = std::make_unique<BurnhopeBuffer>(
        //          device_, sizeof(GlobalUbo), 1,
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

        rebuildBatches(world, *geometryRenderSystem);
        if (!kMinimalRenderPath) {
            buildTLAS(world);
        }
        // Default away from world origin: entity meshes spawn at (0,0,0); camera inside
        // a unit cube is fully back-face culled (VK_CULL_MODE_BACK_BIT) and stays invisible.
        Camera camera(Engine::kWidth, Engine::kHeight, float3{0.0f, 1.5f, 4.0f});
        auto currentTime = std::chrono::high_resolution_clock::now();
        int frameCount = 0;
        auto fpsTimer = currentTime;
        FrameGraph renderPipeline{device_};
        float4x4 prevViewProj = MatrixIdentity();
        float timeAccumulator = 0.0f;
        static std::array<float4x4, 4> cachedCascadeMats;
        static bool matricesCached = false;
        const double targetFPS = 60.0;
        const double maxPeriod = 1.0 / targetFPS;
bool queuedGeometryRebuild = false;

        while (!window_.shouldClose())
        {
            window_.pollEvents();

            bool modelsLoadedThisFrame = false;
            world.each([&](flecs::entity e, MeshComponent &meshComp) {
                if (meshComp.model && meshComp.model->cpuDataReady && !meshComp.model->gpuDataReady) {
                    meshComp.model->finishGpuUpload();

                    if (meshComp.materials.size() < meshComp.model->getSubMeshes().size()) {
                        meshComp.materials.resize(meshComp.model->getSubMeshes().size(),
                                                  defaultWhiteMaterial);
                    }
                    if (meshComp.materialPaths.size() < meshComp.model->getSubMeshes().size()) {
                        meshComp.materialPaths.resize(meshComp.model->getSubMeshes().size(), "");
                    }
                    
                    modelsLoadedThisFrame = true;
                }
            });
            if (modelsLoadedThisFrame) {
                uiManager->GetContext().needsRebuild = true;
            }

            uiManager->ProcessPendingActions();

            bool anyMaterialReloaded = false;
            world.each([&](flecs::entity e, MeshComponent &mc) {
                for (auto& mat : mc.materials) {
                    if (mat && mat->pendingReload) {
                        if (!anyMaterialReloaded) vkDeviceWaitIdle(device_.device());
                        mat->packTextures(
                            device_, bindless_.textures(), bindless_,
                            uiManager->GetContext().textureDeleteQueue);
                        mat->pendingReload = false;
                        anyMaterialReloaded = true;
                        if (mat->needsAnotherPack) mat->packTexturesAsync(); // Если кидали текстуры пока шла упаковка
                    }
                }
            });
            if (anyMaterialReloaded) {
                uiManager->GetContext().needsRebuild = true;
            }

            bool transformsChanged = false;
            std::vector<float3> movedPositions;
            float3 dirtyMin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
            float3 dirtyMax{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
            bool hasDirtyRegion = false;

            std::vector<float4x4> allBoneMatrices;
            allBoneMatrices.reserve(10000); 

            ObjectData* objDataPtr = nullptr;
            if (objectBuffer) {
                objDataPtr = reinterpret_cast<ObjectData*>(objectBuffer->getMappedMemory());
            }
            uint32_t objIndex = 0;
            auto newTime = std::chrono::high_resolution_clock::now();
            float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();

            world.each([&](flecs::entity entity, TransformComponent &tComp, MeshComponent &meshComp) {
                if (!meshComp.model || !meshComp.isVisible || !meshComp.model->gpuDataReady) return;
                
                uint32_t currentBoneOffset = allBoneMatrices.size();
                bool animated = !meshComp.animationPath.empty();
                bool hasBones = false;

                if (animated && !meshComp.skeletonPath.empty()) {
                    meshComp.animationTime += frameTime;
                    auto skel = animationSystem->loadSkeleton(meshComp.skeletonPath);
                    auto anim = animationSystem->loadAnimation(meshComp.animationPath);
                    if (skel && anim && !skel->bones.empty()) {
                        std::vector<float4x4> outMats;
                        animationSystem->evaluate(*skel, *anim, meshComp.animationTime, outMats);
                        if (allBoneMatrices.size() + outMats.size() <= 100000) {
                            allBoneMatrices.insert(allBoneMatrices.end(), outMats.begin(), outMats.end());
                            hasBones = true;
                        }
                    }
                }

                tComp.transform.updateMatrixIfNeeded();
                for (uint32_t i = 0; i < meshComp.model->getSubMeshes().size(); i++) {
                    if (objDataPtr && objIndex < totalSubMeshCount) {
                        // Mesh BDA: row_major mul(p,M,V,P) — always upload DirectX row matrix (see EngineMath.hpp).
                        objDataPtr[objIndex].modelMatrix = tComp.transform.matrix;
                        if (hasBones) {
                            objDataPtr[objIndex].boneOffset = currentBoneOffset;
                        } else {
                            objDataPtr[objIndex].boneOffset = 0xFFFFFFFF;
                        }
                    }
                    objIndex++;
                }
            });

            world.each([&](flecs::entity entity, TransformComponent &tComp) {
                if (tComp.transform.updatematrix) {
                    tComp.transform.updateMatrixIfNeeded();
                    transformsChanged = true;
                    movedPositions.push_back(tComp.transform.position);
                    dirtyMin = Min(dirtyMin, tComp.transform.position - float3{15.0f, 15.0f, 15.0f});
                    dirtyMax = Max(dirtyMax, tComp.transform.position + float3{15.0f, 15.0f, 15.0f});
                    hasDirtyRegion = true;
                    if (entity.has<ReflectionProbeComponent>()) {
                        entity.get_mut<ReflectionProbeComponent>().updateNeeded = true;
                    }
                } 
            });

            if (transformsChanged) {
                world.each([&](flecs::entity entity, LightComponent &lightComp, TransformComponent &lightTrans) {
                    if (!lightComp.needsShadowUpdate) {
                        for (const auto& pos : movedPositions) {
                            if (Length(pos - lightTrans.transform.position) <= lightComp.light.radius + 15.0f) {
                                lightComp.needsShadowUpdate = true;
                                break;
                            }
                        }
                    } 
                });
            }

            if (!allBoneMatrices.empty() && boneMatricesBuffer) {
                size_t maxBytes = boneMatricesBuffer->getBufferSize();
                size_t bytesToWrite = allBoneMatrices.size() * sizeof(float4x4);
                if (bytesToWrite > maxBytes) bytesToWrite = maxBytes;
                boneMatricesBuffer->writeToBuffer(allBoneMatrices.data(), bytesToWrite);
            }

            auto extent = window_.getExtent();
            while (extent.width == 0 || extent.height == 0)
            {
                extent = window_.getExtent();
                window_.pollEvents();
            }
            camera.width = extent.width;
            camera.height = extent.height;
            VkExtent2D swapExtent = renderer_.getSwapChainExtent();
            
            // Проверяем, устарели ли наши текстуры относительно текущего SwapChain,
            // либо был ли явный вызов изменения размера окна от SDL3
            if (hdrOutputTexture == nullptr || 
                hdrOutputTexture->getExtent().width != swapExtent.width || 
                hdrOutputTexture->getExtent().height != swapExtent.height || 
                window_.wasWindowResized())
            {
                vkDeviceWaitIdle(device_.device());
                if (window_.wasWindowResized()) {
                    renderer_.recreateSwapChain();
                    swapExtent = renderer_.getSwapChainExtent();
                    window_.resetWindowResizedFlag();
                }
                VkExtent2D newExtent = swapExtent;
                gBuffer = std::make_unique<BurnhopeGBuffer>(device_, newExtent);
                hdrOutputTexture = std::make_unique<BurnhopeTexture>(device_, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                     VkExtent3D{newExtent.width, newExtent.height, 1},
                                                                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                                     VK_SAMPLE_COUNT_1_BIT);
                ssgiRawTexture = std::make_unique<BurnhopeTexture>(device_, VK_FORMAT_R16G16B16A16_SFLOAT,
                                                                   VkExtent3D{newExtent.width, newExtent.height, 1},
                                                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                                                   VK_SAMPLE_COUNT_1_BIT);
                ssgiRawTexture->transitionLayout(device_.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                rebuildGBufferDescriptorSets();
                if (rcSystem)
                {
                    rcSystem->rebuildOnResize(newExtent);
                }
                if (rtReflectionSystem)
                {
                    rtReflectionSystem->updateDescriptors(newExtent, defaultWhiteTex);
                }
                if (volumetricSystem) {
                    volumetricSystem->updateDescriptors(newExtent);
                    bindless_.slots().volumetric = volumetricSystem->volumetricSampledHeapIndex;
                    bindless_.slots().volumetricStorage = volumetricSystem->volumetricHeapIndex;
                }
                continue;
            }
            const auto &rs = uiManager->GetContext().renderSettings;
            if (rcSystem && rcSystem->needsRebuild(rs.rcProbeGridX, rs.rcProbeGridY, rs.rcProbeGridZ, rs.rcOctaSize, rs.rcBaseRayLength)) {
                vkDeviceWaitIdle(device_.device());
                rcSystem->updateConfig(rs.rcProbeGridX, rs.rcProbeGridY, rs.rcProbeGridZ, rs.rcOctaSize, rs.rcBaseRayLength);
                rcSystem->rebuildOnResize(extent);
            }
          
            //if (frameTime < maxPeriod)
            //{
            //    double sleepTime = maxPeriod - frameTime;
            //    std::this_thread::sleep_for(std::chrono::duration<double>(sleepTime));

                // Пересчитываем время после сна для честного deltaTime
            //    newTime = std::chrono::high_resolution_clock::now();
            //    frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
            //}


            currentTime = newTime;
            timeAccumulator += frameTime;

            if (std::chrono::duration<float>(newTime - fpsTimer).count() >= 1.0f)
            {
                std::cout << "FPS: " << frameCount << "\n";
                frameCount = 0;
                fpsTimer = newTime;
            }
            uint32_t currentSubMeshCount = 0;
            world.each([&](const MeshComponent& meshComp) {
                if (meshComp.model && meshComp.isVisible && meshComp.model->gpuDataReady) {
                    currentSubMeshCount += static_cast<uint32_t>(meshComp.model->getSubMeshes().size());
                }
            });
            if (currentSubMeshCount != totalSubMeshCount) {
                uiManager->GetContext().needsRebuild = true;
            }
            if (uiManager->GetContext().needsRebuild) {
                queuedGeometryRebuild = true;
                uiManager->GetContext().needsRebuild = false;
            }
            if (queuedGeometryRebuild) {
                vkDeviceWaitIdle(device_.device());
                if (!kMinimalRenderPath) {
                    buildPendingBlas(world);
                }
                rebuildBatches(world, *geometryRenderSystem);
                if (!kMinimalRenderPath) {
                    buildTLAS(world);
                }
                queuedGeometryRebuild = false;
            }

            camera.Inputs(window_.getSDLWindow(), frameTime);
            auto commandBuffer = renderer_.beginFrame();
            if (commandBuffer == VK_NULL_HANDLE) {
                if (renderer_.isGpuDeviceLost()) {
                    std::cerr << "[GPU] VkDevice lost — exiting render loop.\n";
                    break;
                }
                if (renderer_.consecutiveGpuFailures() >= 8) {
                    std::cerr << "[GPU] Too many consecutive failures — stop rendering loop.\n";
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

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
                if (!ctx.safeDeleteQueue.empty() || !ctx.textureDeleteQueue.empty())
                {
                    ctx.pendingDeletions.push_back({ctx.safeDeleteQueue, 3});
                    ctx.safeDeleteQueue.clear();
                    ctx.textureDeleteQueue.clear();
                }

                int frameIndex = renderer_.getFrameIndex();
                bindless_.bind(commandBuffer);
                FrameInfo frameInfo{frameIndex, frameTime, commandBuffer, camera, frameGlobalUboHeap_[frameIndex], bindless_};
                shadowSystem->updateLights(world, camera.Position);
                {
                    const auto &uboData = shadowSystem->getLightUBO();
                    lightUboBuffer->writeToBuffer((void *)&uboData);
                    lightUboBuffer->flush();
                }
                if (faceMatricesBuffer) {
                    faceMatricesBuffer->writeToBuffer(
                        const_cast<PointFaceMatrices*>(shadowSystem->getFaceMatricesData()),
                        sizeof(PointFaceMatrices) * 100);
                    faceMatricesBuffer->flush();
                }
                GlobalUbo ubo{};

float currentFov = 45.0f;
                if (uiManager->GetContext().renderSettings.enableDollyZoom) {
                    float direction = uiManager->GetContext().renderSettings.dollyZoomInvert ? -1.0f : 1.0f;
                    currentFov = 45.0f + direction * sin(timeAccumulator * uiManager->GetContext().renderSettings.dollyZoomSpeed) * uiManager->GetContext().renderSettings.dollyZoomIntensity;
                }
                ubo.projection = camera.GetProjectionMatrix(currentFov, 0.1f, 1000.0f);
                
                // --- Micro-Nystagmus (Stress Eye Jitter) ---
                if (rs.enableNystagmus) {
                    float jitterSpeed = 150.0f;
                    float jitterAmount = rs.nystagmusSeverity * 0.01f;
                    float shakeX = sin(timeAccumulator * jitterSpeed) * cos(timeAccumulator * jitterSpeed * 0.8f) * jitterAmount;
                    float shakeY = cos(timeAccumulator * jitterSpeed * 1.2f) * sin(timeAccumulator * jitterSpeed * 0.5f) * jitterAmount;
                    ubo.projection._31 += shakeX;
                    ubo.projection._32 += shakeY;
                }
           
                ubo.view = camera.GetViewMatrix();
                ubo.camPos = camera.Position;
                ubo.zNear = 0.1f;
                ubo.zFar = 1000.0f;
                ubo.screenSize = float4(extent.width, extent.height, 0.f, 0.f);
                ubo.sunDir = shadowSystem->getSunDir();

                ubo.gridDimX = 16;
                ubo.gridDimY = 9;
                ubo.gridDimZ = 24;

                ubo.sunColor = float3(1.0f, 0.95f, 0.8f);
                ubo.sunIntensity = 5.0f;

                ubo.portalID = 0; // Main scene is portal 0
                bool foundDirLight = false;
                world.each([&](LightComponent &lightComp) {
                    if (foundDirLight) return;
                    auto &light = lightComp.light;
                    if (light.enable && light.type == LightType::Directional)
                    {
                        ubo.sunColor = light.color;
                        ubo.sunIntensity = light.intensity;
                        foundDirLight = true;
                    }
                });
                ubo.lightSize = 1.0f;

                // --- ЧИТАЕМ НАСТРОЙКИ ИЗ UIMANAGER ---
                const auto &rs = uiManager->GetContext().renderSettings;

                ubo.sscsParams = float4(rs.enableContactShadows ? 1.0f : 0.0f, rs.contactShadowLength, (float)rs.contactShadowSteps, rs.contactShadowThickness);
                ubo.gtaoParams = float4(rs.enableSSAO ? 1.0f : 0.0f, rs.ssaoRadius, rs.ssaoBias, rs.ssaoIntensity);
                ubo.fogParams = float4(rs.enableFog ? 1.0f : 0.0f, rs.fogDensity, rs.fogHeightFalloff, rs.fogBaseHeight);
                ubo.fogColor = float4(rs.fogColor[0], rs.fogColor[1], rs.fogColor[2], rs.inscatterIntensity);
                ubo.inscatterColor = float4(rs.inscatterColor[0], rs.inscatterColor[1], rs.inscatterColor[2], rs.inscatterPower);
                ubo.skyZenithColor = float4(rs.skyZenithColor[0], rs.skyZenithColor[1], rs.skyZenithColor[2], 1.0f);
                ubo.skyHorizonColor = float4(rs.skyHorizonColor[0], rs.skyHorizonColor[1], rs.skyHorizonColor[2], 1.0f);
                ubo.skySunParams = float4(rs.sunSize, rs.sunGlow, rs.sunGlowSize, 0.0f);
                ubo.ssgiParams = float4(rs.enableSSGI ? 1.0f : 0.0f, (float)rs.ssgiRayCount, rs.ssgiStepSize, rs.ssgiThickness);
                ubo.rtParams = float4(rs.enableRTReflections ? 1.0f : 0.0f, (float)rs.rtMaxBounces, rs.enableRadianceCascades ? 1.0f : 0.0f, 0.0f);

                ubo.prevViewProj = prevViewProj;
                ubo.ppExposureParams = float4(rs.autoExposure ? 1.0f : 0.0f, rs.manualExposure, rs.minBrightness, rs.maxBrightness);
                ubo.ppColorBalance = float4(rs.temperature, rs.contrast, rs.saturation, rs.gamma);
                ubo.ppBloomParams = float4(rs.enableBloom ? 1.0f : 0.0f, rs.bloomThreshold, rs.bloomIntensity, (float)rs.bloomBlurIterations);
                if constexpr (kMinimalRenderPath) {
                    ubo.sscsParams.x = 0.0f;
                    ubo.gtaoParams.x = 0.0f;
                    ubo.fogParams.x = 0.0f;
                    ubo.ssgiParams = float4(0.0f, 0.0f, 0.0f, 0.0f);
                    ubo.rtParams = float4(0.0f, 0.0f, 0.0f, 0.0f);
                    ubo.ppExposureParams = float4(0.0f, 1.0f, 0.5f, 3.0f);
                    ubo.ppColorBalance = float4(6500.0f, 1.0f, 1.0f, 1.0f);
                    ubo.ppBloomParams.x = 0.0f;
                }
                ubo.ppDoFParams = float4(rs.enableDoF ? 1.0f : 0.0f, rs.focusDistance, rs.focusRange, rs.bokehSize);
                ubo.ppVignetteGrain = float4(rs.enableVignette ? rs.vignetteIntensity : 0.0f, rs.enableFilmGrain ? rs.grainIntensity : 0.0f, rs.refractionSpeed * timeAccumulator, rs.enableChromaticAberration ? rs.caIntensity : 0.0f);
                ubo.ppMotionBlur = float4(rs.enableMotionBlur ? 1.0f : 0.0f, rs.mbStrength, timeAccumulator, rs.mbTrails);
                ubo.ppLensFlare = float4(rs.enableLensFlares ? 1.0f : 0.0f, rs.flareIntensity, rs.ghostDispersal, (float)rs.ghosts);
                ubo.ppTAA_CAS = float4(rs.enableTAA ? 1.0f : 0.0f, rs.taaBlendFactor, rs.enableCAS ? 1.0f : 0.0f, rs.casSharpness);
                ubo.ppLensAdvanced = float4(rs.flareHaloWidth, rs.flareChromaticDir, rs.autoFocus ? 1.0f : 0.0f, (float)rs.tonemapper);
                ubo.ppDistortionDirt = float4(rs.enableLensDistortion ? 1.0f : 0.0f, rs.lensDistortionStrength, rs.enableLensDirt ? 1.0f : 0.0f, rs.lensDirtIntensity);
                ubo.ppDitherAniso = float4(rs.enableDithering ? 1.0f : 0.0f, rs.ditherStrength, rs.enableScreenRefraction ? 1.0f : 0.0f, rs.refractionStrength);
                ubo.cgShadows = float4(rs.cgShadows[0], rs.cgShadows[1], rs.cgShadows[2], 1.0f);
                ubo.cgMidtones = float4(rs.cgMidtones[0], rs.cgMidtones[1], rs.cgMidtones[2], 1.0f);
                ubo.cgHighlights = float4(rs.cgHighlights[0], rs.cgHighlights[1], rs.cgHighlights[2], 1.0f);
                ubo.ppRetroParams = float4(rs.enableRetroCRT ? 1.0f : 0.0f, rs.crtScanlines, rs.glitchIntensity, rs.vhsNoise);
                ubo.ppRetroParams2 = float4((float)rs.pixelation, rs.enableVertexJitter ? rs.vertexJitterResolution : 0.0f, (float)rs.bokehShape, rs.bokehAngle);
                
                ubo.ppStylizedParams = float4(rs.enablePosterization ? rs.posterizationLevels : 0.0f, rs.enableKuwahara ? (float)rs.kuwaharaRadius : 0.0f, rs.enableCelShading ? rs.celShadingLevels : 0.0f, rs.enableVoronoi ? rs.voronoiScale : 0.0f);
                
                ubo.ppOutlineParams = float4(rs.enableOutline ? 1.0f : 0.0f, rs.outlineThickness, rs.outlineThresholdDepth, rs.outlineThresholdNormal);
                ubo.ppOutlineColor = float4(rs.outlineColor[0], rs.outlineColor[1], rs.outlineColor[2], (float)rs.outlineMode);
                ubo.ppOutlineJitter = float4(rs.enableOutlineJitter ? 1.0f : 0.0f, rs.outlineJitterSpeed, rs.outlineJitterStrength, rs.objHatchingScale);
                ubo.ppWeatherSSR = float4(rs.enableWeather ? 1.0f : 0.0f, rs.weatherIntensity, rs.enableSSR ? 1.0f : 0.0f, rs.ssrSteps);
                ubo.ppSSSS = float4(rs.enableSSSS ? 1.0f : 0.0f, rs.ssssWidth, rs.ssrThickness, (float)rs.vrsMode);
                ubo.ppWeatherParams = float4(rs.weatherSpeed, rs.weatherSize, rs.weatherDensity, rs.weatherDistortion);

                ubo.cgGlobalLift = float4(rs.cgGlobalLift[0], rs.cgGlobalLift[1], rs.cgGlobalLift[2], rs.cgGlobalLift[3]);
                ubo.cgGlobalGamma = float4(rs.cgGlobalGamma[0], rs.cgGlobalGamma[1], rs.cgGlobalGamma[2], rs.cgGlobalGamma[3]);
                ubo.cgGlobalGain = float4(rs.cgGlobalGain[0], rs.cgGlobalGain[1], rs.cgGlobalGain[2], rs.cgGlobalGain[3]);
                ubo.cgGlobalOffset = float4(rs.cgGlobalOffset[0], rs.cgGlobalOffset[1], rs.cgGlobalOffset[2], rs.cgGlobalOffset[3]);
                ubo.cgShadowsLift = float4(rs.cgShadowsLift[0], rs.cgShadowsLift[1], rs.cgShadowsLift[2], rs.cgShadowsLift[3]);
                ubo.cgShadowsGamma = float4(rs.cgShadowsGamma[0], rs.cgShadowsGamma[1], rs.cgShadowsGamma[2], rs.cgShadowsGamma[3]);
                ubo.cgShadowsGain = float4(rs.cgShadowsGain[0], rs.cgShadowsGain[1], rs.cgShadowsGain[2], rs.cgShadowsGain[3]);
                ubo.cgShadowsOffset = float4(rs.cgShadowsOffset[0], rs.cgShadowsOffset[1], rs.cgShadowsOffset[2], rs.cgShadowsOffset[3]);
                ubo.cgMidtonesLift = float4(rs.cgMidtonesLift[0], rs.cgMidtonesLift[1], rs.cgMidtonesLift[2], rs.cgMidtonesLift[3]);
                ubo.cgMidtonesGamma = float4(rs.cgMidtonesGamma[0], rs.cgMidtonesGamma[1], rs.cgMidtonesGamma[2], rs.cgMidtonesGamma[3]);
                ubo.cgMidtonesGain = float4(rs.cgMidtonesGain[0], rs.cgMidtonesGain[1], rs.cgMidtonesGain[2], rs.cgMidtonesGain[3]);
                ubo.cgMidtonesOffset = float4(rs.cgMidtonesOffset[0], rs.cgMidtonesOffset[1], rs.cgMidtonesOffset[2], rs.cgMidtonesOffset[3]);
                ubo.cgHighlightsLift = float4(rs.cgHighlightsLift[0], rs.cgHighlightsLift[1], rs.cgHighlightsLift[2], rs.cgHighlightsLift[3]);
                ubo.cgHighlightsGamma = float4(rs.cgHighlightsGamma[0], rs.cgHighlightsGamma[1], rs.cgHighlightsGamma[2], rs.cgHighlightsGamma[3]);
                ubo.cgHighlightsGain = float4(rs.cgHighlightsGain[0], rs.cgHighlightsGain[1], rs.cgHighlightsGain[2], rs.cgHighlightsGain[3]);
                ubo.cgHighlightsOffset = float4(rs.cgHighlightsOffset[0], rs.cgHighlightsOffset[1], rs.cgHighlightsOffset[2], rs.cgHighlightsOffset[3]);
                ubo.cgRgbMixerRed = float4(rs.cgRgbMixerRed[0], rs.cgRgbMixerRed[1], rs.cgRgbMixerRed[2], 0.0f);
                ubo.cgRgbMixerGreen = float4(rs.cgRgbMixerGreen[0], rs.cgRgbMixerGreen[1], rs.cgRgbMixerGreen[2], 0.0f);
                ubo.cgRgbMixerBlue = float4(rs.cgRgbMixerBlue[0], rs.cgRgbMixerBlue[1], rs.cgRgbMixerBlue[2], 0.0f);
                ubo.ppBlurs = float4(rs.blurMode, rs.blurStrength, rs.blurRadius, 0.0f);
                ubo.ppBlurCenter = float4(rs.radialBlurCenter[0], rs.radialBlurCenter[1], rs.enableAnamorphic ? 1.0f : 0.0f, 0.0f);
                ubo.ppColorFX = float4(rs.enableColorInvert ? 1.0f : 0.0f, rs.enableFalseColor ? 1.0f : 0.0f, rs.enableDepthView ? 1.0f : 0.0f, rs.enableFilmDamage ? 1.0f : 0.0f);
                ubo.ppFilmDamage = float4(rs.filmDamageIntensity, rs.filmDamageScratches, 0.0f, 0.0f);
                ubo.ppEdgeDetect = float4(rs.enableEdgeDetect ? 1.0f : 0.0f, rs.edgeWidth, rs.edgeBrightness, rs.edgeGamma);
                ubo.ppEdgeDetect2 = float4(rs.edgeBlur, (float)rs.edgeColorMode, 0.0f, 0.0f);
                ubo.ppEdgeColor = float4(rs.edgeCustomColor[0], rs.edgeCustomColor[1], rs.edgeCustomColor[2], 1.0f);
                ubo.ppEmboss = float4(rs.enableEmboss ? 1.0f : 0.0f, rs.embossStrength, rs.embossAngle, (float)rs.embossStyle);
                ubo.ppSketch = float4(rs.enableSketch ? 1.0f : 0.0f, rs.sketchStrokeStrength, rs.sketchStrokeLength, rs.sketchThreshold);
                ubo.ppSketch2 = float4(rs.sketchShadowLevel, rs.sketchShadowsWeight, rs.sketchMidtonesWeight, rs.sketchHighlightsWeight);
                ubo.ppHalftone = float4(rs.enableHalftone ? 1.0f : 0.0f, rs.halftoneScale, rs.halftoneContrast, rs.halftoneTex ? 1.0f : 0.0f);
                ubo.ppDitherData = float4((float)rs.ditherMode, rs.ditherScale, 0.0f, 0.0f);
                ubo.ditherShadow = float4(rs.ditherShadowColor[0], rs.ditherShadowColor[1], rs.ditherShadowColor[2], 1.0f);
                ubo.ditherMid = float4(rs.ditherMidColor[0], rs.ditherMidColor[1], rs.ditherMidColor[2], 1.0f);
                ubo.ditherHighlight = float4(rs.ditherHighlightColor[0], rs.ditherHighlightColor[1], rs.ditherHighlightColor[2], 1.0f);
                ubo.ppWarp = float4(rs.enableTexWarp ? 1.f : 0.f, rs.texWarpStrength, rs.texWarpSpeed, rs.enableVtxWarp ? 1.f : 0.f);
                ubo.ppWarp2 = float4(rs.vtxWarpStrength, rs.vtxWarpSpeed, rs.vtxWarpScale, 0.f);
                ubo.ppColorComp = float4(rs.enableColorComp ? 1.f : 0.f, rs.colorCompLevels, rs.enableCMAA ? 1.f : 0.f, rs.paletteTex ? 1.f : 0.f);
                ubo.shadowRampColor1 = float4(rs.shadowRampColor1[0], rs.shadowRampColor1[1], rs.shadowRampColor1[2], rs.enableShadowRamp ? 1.f : 0.f);
                ubo.shadowRampColor2 = float4(rs.shadowRampColor2[0], rs.shadowRampColor2[1], rs.shadowRampColor2[2], 1.f);
                ubo.ppBleedMosh = float4(rs.enableOpticalSoup ? 1.f : 0.f, rs.opticalSoupRadius, rs.enableDatamosh ? 1.f : 0.f, rs.datamoshThreshold);
                ubo.ppAsciiSort = float4(rs.enableAscii ? 1.f : 0.f, rs.asciiScale, rs.enablePixelSort ? 1.f : 0.f, rs.pixelSortThreshold);
                ubo.ppImpact = float4(rs.enableImpactFrame ? 1.f : 0.f, rs.impactSize, rs.impactPower, rs.impactTime);
                ubo.ppTrails = float4(rs.enableSmearTrails ? 1.f : 0.f, rs.smearLength, rs.smearThreshold, 0.f);
                ubo.ppPixelSort = float4(rs.pixelSortAngle, rs.pixelSortLength, rs.pixelSortTime, (float)rs.asciiMode);
                ubo.ppArtistic = float4(rs.enableRimLight ? 1.f : 0.f, rs.rimThickness, rs.rimPower, 0.f);
                ubo.ppArtisticColor = float4(rs.rimColor[0], rs.rimColor[1], rs.rimColor[2], 1.f);
                
                ubo.ppStylized3 = float4(rs.enableScreentones ? 1.f : 0.f, rs.screentoneSize, rs.screentoneDarkness, rs.enableWatercolor ? 1.f : 0.f);
                ubo.ppStylized4 = float4(rs.watercolorRadius, rs.enablePointillism ? 1.f : 0.f, rs.pointillismSize, rs.pointillismDensity);
                ubo.ppLens3 = float4(rs.enableTiltShift ? 1.f : 0.f, rs.tiltShiftAmount, rs.tiltShiftFalloff, rs.enableLensBreathing ? 1.f : 0.f);
                ubo.ppLens4 = float4(rs.lensBreathingScale, rs.enableStarFilter ? 1.f : 0.f, rs.starFilterThreshold, rs.starFilterLength);
                ubo.ppGlitch3 = float4(rs.enableLightLeaks ? 1.f : 0.f, rs.lightLeakIntensity, rs.enableJpegArtifacts ? 1.f : 0.f, rs.jpegBlockSize);
                ubo.ppGlitch4 = float4(rs.jpegQuality, rs.enableScreenTear ? 1.f : 0.f, rs.screenTearFrequency, rs.screenTearIntensity);
                ubo.gbColor1 = float4(rs.gbColor1[0], rs.gbColor1[1], rs.gbColor1[2], rs.enableGameBoy ? 1.f : 0.f);
                ubo.gbColor2 = float4(rs.gbColor2[0], rs.gbColor2[1], rs.gbColor2[2], 0.f);
                ubo.gbColor3 = float4(rs.gbColor3[0], rs.gbColor3[1], rs.gbColor3[2], 0.f);
                ubo.gbColor4 = float4(rs.gbColor4[0], rs.gbColor4[1], rs.gbColor4[2], 0.f);
                
                ubo.ppSpeedLines = float4(rs.enableSpeedLines ? 1.f : 0.f, rs.speedLinesIntensity, rs.enableColorSplash ? 1.f : 0.f, rs.splashHue);
                ubo.ppColorSplash = float4(rs.splashRange, rs.enableHeatShimmer ? 1.f : 0.f, rs.heatIntensity, 0.f);
                ubo.ppHeatFrost = float4(rs.enableFrost ? rs.frostIntensity : 0.f, rs.enableWaterDrops ? 1.f : 0.f, rs.dropRefraction, rs.enableTemporalEcho ? 1.f : 0.f);
                ubo.ppDropsEcho = float4(rs.echoFade, rs.enableCanvas ? 1.f : 0.f, rs.canvasIntensity, rs.enableInkBleed ? 1.f : 0.f);
                ubo.ppCanvasInk = float4(rs.inkRadius, rs.enableWorldCurve ? 1.f : 0.f, rs.curveAmount, rs.enableGlitter ? 1.f : 0.f);
                ubo.ppWorldGlitter = float4(rs.glitterThreshold, rs.enableCaustics ? 1.f : 0.f, rs.causticsSpeed, rs.causticsScale);
                ubo.ppCausticsBreath = float4(rs.causticsStrength, rs.enableBreathing ? 1.f : 0.f, rs.breathAmplitude, rs.breathSpeed);
                
              
                
                ubo.ppTransAnime = float4(rs.translucencyStrength, rs.enableAnimeSpecular ? 1.f : 0.f, rs.animeSpecBands, rs.enableAstigmatism ? 1.f : 0.f);
                ubo.ppAstigDolly = float4(rs.astigmatismLength, rs.astigmatismAngle, rs.enableSaccadicMasking ? 1.f : 0.f, rs.saccadicThreshold);
                ubo.ppSaccBurn = float4(rs.enableBurningFilm ? 1.f : 0.f, timeAccumulator * 0.1f, rs.enablePhosphor ? 1.f : 0.f, rs.phosphorFade);
                ubo.ppPhosASCII = float4(rs.enableWorldASCII ? 1.f : 0.f, rs.worldAsciiScale, rs.enableGravityLensing ? 1.f : 0.f, rs.gravityMass);
                ubo.ppGravVector = float4(rs.enableVectorFlow ? 1.f : 0.f, rs.vectorFlowStrength, rs.enableKMeans ? 1.f : 0.f, rs.kMeansColors);
                ubo.ppKMeansFeed = float4(rs.enableRecursiveFeedback ? 1.f : 0.f, rs.feedbackZoom, rs.feedbackAngle, rs.enableCrosshatchLight ? 1.f : 0.f);
                ubo.ppHatchAnalog = float4(rs.bayerWorldSpace ? 1.f : 0.f, rs.enableAnalogNoise ? 1.f : 0.f, rs.analogSyncLoss, rs.enableScanlineMoire ? 1.f : 0.f);
                ubo.ppMoireTunnel = float4(rs.moireScale, rs.enableTunnelVision ? 1.f : 0.f, rs.tunnelIntensity, rs.enableAfterimage ? 1.f : 0.f);
                ubo.ppAfterBleed = float4(rs.afterimageFade, rs.enableTemporalBleed ? 1.f : 0.f, rs.bleedSpeed, rs.enableFluidSim ? 1.f : 0.f);
                ubo.ppFluidCMYK = float4(rs.fluidSpeed, rs.enableCMYK ? 1.f : 0.f, rs.cmykOffset, rs.enableCondensation ? 1.f : 0.f);
                ubo.ppCondenDust = float4(rs.condensationAmount, rs.enableDustMotes ? 1.f : 0.f, rs.dustIntensity, rs.enableEctoplasm ? 1.f : 0.f);
                ubo.ppEctoRolling = float4(rs.ectoplasmColor[0], rs.ectoplasmColor[1], rs.ectoplasmColor[2], rs.enableRollingShutter ? 1.f : 0.f);
                ubo.ppPurkinjeSlit = float4(rs.rollingShutterSpeed, rs.enablePurkinje ? 1.f : 0.f, rs.purkinjeIntensity, rs.enableSlitScan ? 1.f : 0.f);
                ubo.ppReactDroste = float4(rs.slitScanSpeed, rs.enableReactionDiffusion ? 1.f : 0.f, rs.rdSpeed, rs.enableDroste ? rs.drosteScale : 0.f);

                ubo.ppPsych1 = float4(rs.enableBlinking ? rs.blinkFrequency : 0.f, rs.enableFloaters ? rs.floatersOpacity : 0.f, rs.enableTimeStutter ? rs.stutterSeverity : 0.f, rs.enableHollowFace ? 1.f : 0.f);
                ubo.ppPsych2 = float4(rs.enableMelting ? rs.meltSpeed : 0.f, rs.enableAntiLight ? 1.f : 0.f, rs.enableTrypo ? rs.trypoScale : 0.f, rs.enableParallaxEye ? 1.f : 0.f);
                ubo.ppPsych3 = float4(rs.enableInsideSmudges ? rs.smudgeIntensity : 0.f, rs.enableHaunting ? rs.hauntingTrail : 0.f, rs.enableNystagmus ? rs.nystagmusSeverity : 0.f, rs.enablePurkinje ? rs.purkinjeScale : 0.f);
                ubo.ppPsych4 = float4(rs.enableRodCone ? 1.f : 0.f, rs.enableFluidLens ? rs.fluidViscosity : 0.f, rs.stutterSpeed, 0.f);

                ubo.ppPsych5 = float4(rs.meltThreshold, rs.meltNoiseScale, rs.hollowFaceDepth, 0.f);
                ubo.ppPsych6 = float4(rs.rodConeThreshold, rs.rodConeColor[0], rs.rodConeColor[1], rs.rodConeColor[2]);
                ubo.ppPsych7 = float4(rs.purkinjeThickness, rs.purkinjeColor[0], rs.purkinjeColor[1], rs.purkinjeColor[2]);
                ubo.ppPsych8 = float4(rs.purkinjeSpeed, rs.vectorFieldScale, rs.speedLinesCount, rs.speedLinesLength);
                
                ubo.ppCausticsScale = float4(rs.causticsScale, rs.enableTranslucency ? 1.f : 0.f, rs.vectorTexIdx, rs.canvasTexIdx);
                ubo.ppTexIndices = float4(rs.vectorTexIdx, rs.causticsTexIdx, rs.canvasTexIdx, -1.0f);

                // Сохраняем чистую проекцию ДО тряски для следующего кадра
                float4x4 unjitteredProj = ubo.projection;

                // Halton Jitter for TAA
                if (rs.enableTAA) {
                    float halton2[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
                    float halton3[8] = {0.333f, 0.666f, 0.111f, 0.444f, 0.777f, 0.222f, 0.555f, 0.888f};
                    float jitterX = (halton2[frameCount % 8] - 0.5f) / (float)extent.width;
                    float jitterY = (halton3[frameCount % 8] - 0.5f) / (float)extent.height;
                    ubo.projection._31 += jitterX * 2.0f;
                    ubo.projection._32 += jitterY * 2.0f;
                }

                // GPU: mul(matrix, column). CPU row-vectors: v * View * Projection.
                ubo.invViewProj = gpuInvViewProjection(ubo.view, ubo.projection);

                static float3 prevCamPos = float3{0.0f, 0.0f, 0.0f};
                static float4x4 prevView = MatrixIdentity();
                static float3 prevSunDir = float3{0.0f, 0.0f, 0.0f};
                static float3 prevCamDir = float3{0.0f, 0.0f, 0.0f};
                static bool firstFrame = true;

                bool csmNeedsFullUpdate = false;
                // CSM жестко привязан к камере. Если камера двигается или вращается - обязательно обновляем тени!
                if (firstFrame || prevSunDir != shadowSystem->getSunDir() || Length(prevCamPos - camera.Position) > 0.05f || Length(prevCamDir - camera.Orientation) > 0.005f)
                {
                    csmNeedsFullUpdate = true;
                    firstFrame = false;
                    prevCamPos = camera.Position;
                    prevCamDir = camera.Orientation;
                }
                prevView = ubo.view;
                prevSunDir = shadowSystem->getSunDir();
                
                prevViewProj = gpuPrevViewProjection(ubo.view, unjitteredProj);

                const float4x4 meshProjRow = unjitteredProj;
                const float4x4 meshViewRow = ubo.view;

                if constexpr (!kGpuRowVectorMul) {
                    ubo.projection = toGpuMatrix(ubo.projection);
                    ubo.view = toGpuMatrix(ubo.view);
                }

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
                ubo.cascadeSplits = float4(shadowSystem->cascadeSplits[0], shadowSystem->cascadeSplits[1], shadowSystem->cascadeSplits[2], shadowSystem->cascadeSplits[3]);
                frameUboBuffers_[frameIndex]->writeToBuffer(&ubo);
                frameUboBuffers_[frameIndex]->flush();
                
                if (decalBuffer) {
                    DecalBlock db{}; db.decalCount = 0;
                    world.each([&](TransformComponent& trans, DecalComponent& decal) {
                        if (db.decalCount < 1000) {
                            trans.transform.updateMatrixIfNeeded();
                            db.decals[db.decalCount].invModelMatrix = MatrixInverse(trans.transform.matrix);
                            db.decals[db.decalCount].params = float4((float)decal.albedoTexIdx, (float)decal.normalTexIdx, decal.opacity, 0.0f);
                            db.decalCount++;
                        }
                    });
                    decalBuffer->writeToBuffer(&db);
                    decalBuffer->flush();
                }
                
                ProbesInfo pInfo{};
                pInfo.count = 0;
                world.each([&](flecs::entity e, TransformComponent &t, ReflectionProbeComponent &p) {
                    if (pInfo.count >= 16) return;
                    if (p.textureIndex == -1) {
                        p.textureIndex = static_cast<int>(rtReflectionSystem->probeTextures.size());
                        auto tex = std::make_unique<BurnhopeTexture>(device_, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{(uint32_t)p.resolution, (uint32_t)p.resolution, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
                        tex->transitionLayout(device_.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
                        rtReflectionSystem->probeTextures.push_back(std::move(tex));
                    }
                    if (t.transform.updatematrix || p.updateNeeded) {
                        p.updateNeeded = true; 
                    }
                    pInfo.data[pInfo.count].positionAndRadius = MakeFloat4(t.transform.position, p.radius);
                    pInfo.count++; });
                if (rtReflectionSystem->probesBuffer)
                {
                    rtReflectionSystem->probesBuffer->writeToBuffer(&pInfo);
                    rtReflectionSystem->probesBuffer->flush();
                }

                // This buffer will be filled during the G-Buffer pass for use in the later lighting pass.
                auto* mappedPortalUbos = reinterpret_cast<GlobalUbo*>(portalUbosBuffer->getMappedMemory());


                renderPipeline.clear();
                if (kMinimalRenderPath) {
                    static bool slotLogOnce = false;
                    if (!slotLogOnce) {
                        const auto& sl = slots();
                        std::cerr << "[Minimal] sizeof(GlobalUbo)=" << sizeof(GlobalUbo)
                                  << " rowVectorMul=" << (kGpuRowVectorMul ? 1 : 0)
                                  << " debugStage=" << kMinimalDebugStage
                                  << " gfxDebug=0x" << std::hex << packGfxDebugFlags() << std::dec
                                  << " (fragMode=" << kGBufferDebugFragMode
                                  << " forceNdc=" << (kMeshForceNdcTriangle ? 1 : 0) << ")"
                                  << " cpuLodTri=" << (totalSubMeshCount > 0 && objectBuffer
                                                         ? objectBuffer->getMappedMemory()
                                                               ? reinterpret_cast<const ObjectData*>(
                                                                     objectBuffer->getMappedMemory())[0]
                                                                     .indexCount /
                                                                 3u
                                                               : 0u
                                                         : 0u)
                                  << '\n';
                        std::cerr << "[Minimal] subMeshes=" << totalSubMeshCount
                                  << " gbufferAlbedo=" << sl.gbufferAlbedo
                                  << " gbufferDepth=" << sl.gbufferDepth
                                  << " objectStorage=" << sl.objectStorage
                                  << " materialStorage=" << sl.materialStorage
                                  << " globalUbo=" << sl.globalUbo[frameIndex % 3] << '\n';
                        slotLogOnce = true;
                    }
                }
                if (!kMinimalRenderPath) {
               renderPipeline.addPass("Prepare Shadow Maps", {
                    FrameGraph::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT),
                    FrameGraph::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)
                }, [](VkCommandBuffer cmd) {});
                renderPipeline.addPass("Shadow Maps Pass", {}, [&](VkCommandBuffer cmd)
                                       {
                    for (int i = 0; i < BurnhopeCSM::CASCADE_COUNT; i++) {
                        VkRenderingAttachmentInfo depthAttachment{};
                        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        depthAttachment.imageView = shadowSystem->getCSM()->getCascadeView(i);
                        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                        depthAttachment.loadOp = csmNeedsFullUpdate ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                        depthAttachment.clearValue.depthStencil = {1.0f, 0};
                        
                        VkRenderingInfo renderingInfo{};
                        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                        renderingInfo.renderArea.extent = { BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE };
                        renderingInfo.layerCount = 1;
                        renderingInfo.pDepthAttachment = &depthAttachment;
                         renderingInfo.pStencilAttachment = &depthAttachment;
                        
                        vkCmdBeginRendering(cmd, &renderingInfo);
                        vkCmdSetCullMode(cmd, VK_CULL_MODE_FRONT_BIT);
                        vkCmdSetDepthTestEnable(cmd, VK_TRUE);
                        vkCmdSetDepthWriteEnable(cmd, VK_TRUE);
                        vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
                        
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
                            
                            ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, cachedCascadeMats[i], world, false);
                        } else if (hasDirtyRegion) {
                            // Локальное кэширование: Очищаем и рисуем только в том месте, где объекты двигались!
                            float3 corners[8] = {
                                {dirtyMin.x, dirtyMin.y, dirtyMin.z}, {dirtyMax.x, dirtyMin.y, dirtyMin.z},
                                {dirtyMin.x, dirtyMax.y, dirtyMin.z}, {dirtyMax.x, dirtyMax.y, dirtyMin.z},
                                {dirtyMin.x, dirtyMin.y, dirtyMax.z}, {dirtyMax.x, dirtyMin.y, dirtyMax.z},
                                {dirtyMin.x, dirtyMax.y, dirtyMax.z}, {dirtyMax.x, dirtyMax.y, dirtyMax.z}
                            };
                            float minX = 1.0f, minY = 1.0f, maxX = 0.0f, maxY = 0.0f;
                            for(int k=0; k<8; k++) {
                                float4 pt = cachedCascadeMats[i] * MakeFloat4(corners[k], 1.0f);
                                float3 ndc = MakeFloat3(pt) / pt.w;
                                float2 uv = MakeFloat2(ndc) * 0.5f + float2{0.5f, 0.5f};
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

                               ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, cachedCascadeMats[i], world, false);
                            }
                        }
                        vkCmdEndRendering(cmd);
                    }
                    
                    VkRenderingAttachmentInfo atlasAttachment{};
                    atlasAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    atlasAttachment.imageView = shadowSystem->getAtlas()->getTexture()->getImageView();
                    atlasAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    atlasAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    atlasAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    VkRenderingInfo renderingAtlasInfo{};
                    renderingAtlasInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    renderingAtlasInfo.renderArea.extent = { BurnhopeShadowAtlas::ATLAS_RESOLUTION, BurnhopeShadowAtlas::ATLAS_RESOLUTION };
                    renderingAtlasInfo.layerCount = 1;
                    renderingAtlasInfo.pDepthAttachment = &atlasAttachment;
                    renderingAtlasInfo.pStencilAttachment = &atlasAttachment;
                    vkCmdBeginRendering(cmd, &renderingAtlasInfo);
                    
                    world.each([&](LightComponent& lightComp, TransformComponent& transComp) {
                        auto& light = lightComp.light;
                        auto& trans = transComp.transform;
                        
                        if (!light.enable || !light.castShadows || light.type == LightType::Directional || light.shadowSlot < 0 || !lightComp.needsShadowUpdate) return;

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

                            ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, light.lightSpaceMatrix, world, false);
                        }
                        else if (light.type == LightType::Point) {
                            float3 pos = trans.position;
                            float4x4 proj = MatrixPerspectiveFovLH(Radians(90.0f), 1.0f, 0.1f, light.radius);
                            const float3 dirs[6] = {
                                {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
                            };
                            const float3 ups[6] = {
                                {0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}
                            };  
                            for (int face = 0; face < 6; face++) {
                                int fx = pxX + face * tileSize;
                                int fy = pxY;
                                if (fx + tileSize > BurnhopeShadowAtlas::ATLAS_RESOLUTION) {
                                    fx = fx % BurnhopeShadowAtlas::ATLAS_RESOLUTION; fy += tileSize;
                                }
                                    float4x4 faceProj = MatrixPerspectiveFovLH(Radians(90.0f), 1.0f, 0.1f, light.radius);
                                    float4x4 faceView = MatrixLookAtLH(pos, pos + dirs[face], ups[face]);
                                    float4x4 faceMatrix = faceProj * faceView;
                                shadowSystem->getAtlas()->setTileViewport(cmd, fx, fy, tileSize);

                                VkClearRect clearRect{};
                                clearRect.rect.offset = { fx, fy };
                                clearRect.rect.extent = { (uint32_t)tileSize, (uint32_t)tileSize };
                                clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                                vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                                ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, faceMatrix, world, false);
                            }
                        }
                        lightComp.needsShadowUpdate = false; // Кэшируем до следующих изменений
                    });
                       vkCmdEndRendering(cmd); });
                // renderPipeline.addPass("Frustum Culling", [&](VkCommandBuffer cmd) { ... });
                // Отключено: Culling теперь выполняется аппаратно в Task-шейдерах (VK_EXT_mesh_shader)
 renderPipeline.addPass("Resolve Shadows", {
                    FrameGraph::createImageBarrier(shadowSystem->getCSM()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT| VK_IMAGE_ASPECT_STENCIL_BIT, BurnhopeCSM::CASCADE_COUNT),
                    FrameGraph::createImageBarrier(shadowSystem->getAtlas()->getTexture()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)
                }, [](VkCommandBuffer cmd){});
                renderPipeline.addPass("Light Culling Pass", {}, [&](VkCommandBuffer cmd)
                                       {
                    vkCmdFillBuffer(cmd, dummyIndexBuffer->getBuffer(), 0, 4, 0);
                    vkCmdFillBuffer(cmd, dummyGridBuffer->getBuffer(), 0, VK_WHOLE_SIZE, 0);

                    VkBufferMemoryBarrier2 clearBarriers[2]{};
                    for (VkBufferMemoryBarrier2& b : clearBarriers) {
                        b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                        b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                    }
                    clearBarriers[0].buffer = dummyIndexBuffer->getBuffer();
                    clearBarriers[0].size = 4;
                    clearBarriers[1].buffer = dummyGridBuffer->getBuffer();
                    clearBarriers[1].size = VK_WHOLE_SIZE;

                    VkDependencyInfo depInfo1{};
                    depInfo1.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depInfo1.bufferMemoryBarrierCount = 2;
                    depInfo1.pBufferMemoryBarriers = clearBarriers;
                    vkCmdPipelineBarrier2(cmd, &depInfo1);

                    lightCullingShader->bind(cmd);
                    {
                        LightCullHeapPC lcPush = makeLightCullPush(slots(), frameIndex);
                        lightCullingShader->pushConstants(cmd, bindless_, &lcPush, sizeof(lcPush));
                    }
                    lightCullingShader->dispatch(cmd, (16 + 7) / 8, (9 + 7) / 8, (24 + 7) / 8); 

                    // Ожидаем завершения кластеризации перед освещением
                    VkBufferMemoryBarrier2 gridBarriers[2];
                    gridBarriers[0] = clearBarriers[0]; 
                    gridBarriers[0].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    gridBarriers[0].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT; 
                    gridBarriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    gridBarriers[0].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT; 
                    gridBarriers[0].buffer = dummyGridBuffer->getBuffer(); 
                    gridBarriers[0].size = VK_WHOLE_SIZE;
                    
                    gridBarriers[1] = clearBarriers[1]; 
                    gridBarriers[1].srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    gridBarriers[1].srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT; 
                    gridBarriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    gridBarriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT; 
                    gridBarriers[1].buffer = dummyIndexBuffer->getBuffer(); 
                    gridBarriers[1].size = VK_WHOLE_SIZE;

                    VkDependencyInfo depInfo2{};
                    depInfo2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depInfo2.bufferMemoryBarrierCount = 2;
                    depInfo2.pBufferMemoryBarriers = gridBarriers;
                    vkCmdPipelineBarrier2(cmd, &depInfo2); });
                } // !kMinimalRenderPath (shadows + light culling)

                const bool runScenePasses =
                    !kMinimalRenderPath || kMinimalDebugStage != 1;
                if (runScenePasses) {
                renderPipeline.addPass("Prepare G-Buffer", {
                    FrameGraph::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getEmissive()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getPortalID()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                }, [](VkCommandBuffer cmd) {});
                
                renderPipeline.addPass("G-Buffer Pass", {}, [&](VkCommandBuffer cmd)
                                       {
                    std::array<VkRenderingAttachmentInfo, 5> colorAttachments{};
                    for (int i = 0; i < 5; i++) {
                        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                        colorAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                        colorAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    }
                    colorAttachments[0].imageView = gBuffer->getNormalRoughness()->getImageView(); colorAttachments[0].clearValue.color = {0.f, 0.f, 0.f, 0.f};
                    colorAttachments[1].imageView = gBuffer->getAlbedoMetallic()->getImageView();
                    colorAttachments[1].clearValue.color = {0.f, 0.f, 0.f, 0.f};
                    colorAttachments[2].imageView = gBuffer->getHeightAO()->getImageView(); colorAttachments[2].clearValue.color = {0.f, 0.f, 0.f, 0.f};
                    // Emissive.a = cleared depth (1.0) for sky; matches depth attachment clear.
                    colorAttachments[3].imageView = gBuffer->getEmissive()->getImageView(); colorAttachments[3].clearValue.color = {0.f, 0.f, 0.f, 1.f};
                    colorAttachments[4].imageView = gBuffer->getPortalID()->getImageView(); colorAttachments[4].clearValue.color.uint32[0] = 0;

                    VkRenderingAttachmentInfo depthAttachment{};
                    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    depthAttachment.imageView = gBuffer->getDepth()->getImageView();
                    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    depthAttachment.clearValue.depthStencil = {1.0f, 0};

                    VkRenderingInfo renderingInfo{};
                    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    renderingInfo.renderArea.extent = extent;
                    renderingInfo.layerCount = 1;
                    renderingInfo.colorAttachmentCount = 5;
                    renderingInfo.pColorAttachments = colorAttachments.data();
                    renderingInfo.pDepthAttachment = &depthAttachment;
                    renderingInfo.pStencilAttachment = &depthAttachment;

                    vkCmdBeginRendering(cmd, &renderingInfo);

                    {
                        PipelineConfigInfo gbufOutCfg{};
                        BurnhopePipeline::defaultPipelineConfigInfo(gbufOutCfg);
                        gbufOutCfg.colorBlendAttachments.resize(5);
                        for (auto& a : gbufOutCfg.colorBlendAttachments) {
                            a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                            a.blendEnable = VK_FALSE;
                        }
                        gbufOutCfg.colorBlendInfo.attachmentCount =
                            static_cast<uint32_t>(gbufOutCfg.colorBlendAttachments.size());
                        gbufOutCfg.colorBlendInfo.pAttachments = gbufOutCfg.colorBlendAttachments.data();
                        gbufOutCfg.colorAttachmentFormats = {
                            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
                            VK_FORMAT_R8_UINT};
                        BurnhopePipeline::applyFragmentOutputState(device_.device(), cmd, gbufOutCfg);
                    }

                    if constexpr (kMinimalRenderPath) {
                        std::vector<VkBufferMemoryBarrier2> meshBufBars;
                        meshBufBars.reserve(12);
                        world.each([&](const MeshComponent& meshComp) {
                            if (!meshComp.model || !meshComp.model->gpuDataReady) {
                                return;
                            }
                            const auto addBarrier = [&](VkBuffer buffer, VkDeviceSize size) {
                                if (buffer == VK_NULL_HANDLE || size == 0) {
                                    return;
                                }
                                VkBufferMemoryBarrier2& b = meshBufBars.emplace_back();
                                b.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                                b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                                b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                                b.dstStageMask = VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT |
                                                 VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                                                 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                                b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                                b.buffer = buffer;
                                b.size = size;
                            };
                            const uint32_t verts = meshComp.model->getVertexCount();
                            addBarrier(meshComp.model->getPosVkBuffer(),
                                       static_cast<VkDeviceSize>(verts) * sizeof(PackedVertexPos));
                            addBarrier(meshComp.model->getAttrVkBuffer(),
                                       static_cast<VkDeviceSize>(verts) * sizeof(PackedVertexAttr));
                            addBarrier(meshComp.model->getIndexVkBuffer(),
                                       static_cast<VkDeviceSize>(meshComp.model->indexCount) *
                                           sizeof(uint32_t));
                        });
                        if (!meshBufBars.empty()) {
                            VkDependencyInfo meshDep{};
                            meshDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                            meshDep.bufferMemoryBarrierCount =
                                static_cast<uint32_t>(meshBufBars.size());
                            meshDep.pBufferMemoryBarriers = meshBufBars.data();
                            vkCmdPipelineBarrier2(cmd, &meshDep);
                        }
                    }

                    if (objectBuffer) {
                        VkBufferMemoryBarrier2 objBufBar{};
                        objBufBar.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        objBufBar.srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
                        objBufBar.srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT;
                        objBufBar.dstStageMask =
                            VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT |
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                        objBufBar.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                        objBufBar.buffer = objectBuffer->getBuffer();
                        objBufBar.size = objectBuffer->bufferSize();
                        VkDependencyInfo objDep{};
                        objDep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        objDep.bufferMemoryBarrierCount = 1;
                        objDep.pBufferMemoryBarriers = &objBufBar;
                        vkCmdPipelineBarrier2(cmd, &objDep);
                    }

                    GfxHeapPush gfxPushG = makeGfxPushForDraw(
                        slots(), frameIndex, device_, objectBuffer.get(), totalSubMeshCount);
                    gfxPushG.meshProjection = meshProjRow;
                    gfxPushG.meshView = meshViewRow;
                    {
                        const uint32_t uboFi = frameIndex % BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT;
                        SceneAddresses scene{};
                        scene.globalUboHeap = slots().globalUbo[uboFi];
                        scene.textureTableBase = slots().textureTableBase;
                        if (objectBuffer) {
                            scene.objectStorageBda =
                                deviceAddressBits(objectBuffer->deviceAddress());
                        }
                        if (materialBuffer) {
                            scene.materialStorageBda =
                                deviceAddressBits(materialBuffer->deviceAddress());
                        }
                        if (boneMatricesBuffer) {
                            scene.boneStorageBda =
                                deviceAddressBits(boneMatricesBuffer->deviceAddress());
                        }
                        scene.defaultSampler = slots().defaultSampler;
                        scene.textureTableCount = bindless_.textures().count();
                        if (frameUboBuffers_[uboFi]) {
                            scene.globalUboBda =
                                deviceAddressBits(frameUboBuffers_[uboFi]->deviceAddress());
                        }
                        sceneAddressesBuffer->writeToBuffer(&scene);
                        sceneAddressesBuffer->flush();
                        gfxPushG.sceneAddressesBda =
                            deviceAddressBits(sceneAddressesBuffer->deviceAddress());
                        // Frag reads heap/BDA via SceneAddresses BDA; push copies kept for debug/tools.
                        gfxPushG.globalUboHeap = scene.globalUboHeap;
                        gfxPushG.defaultSampler = scene.defaultSampler;
                        gfxPushG.textureTableCount = scene.textureTableCount;
                        gfxPushG.materialStorageBda = scene.materialStorageBda;
                        if constexpr (kMinimalRenderPath) {
                          static bool sceneVerifyOnce = false;
                          if (!sceneVerifyOnce) {
                            const auto* mapped = static_cast<const SceneAddresses*>(
                                sceneAddressesBuffer->getMappedMemory());
                            if (mapped != nullptr) {
                              std::cerr << "[SceneVerify] sizeof=" << sizeof(SceneAddresses)
                                        << " globalUboHeap=" << mapped->globalUboHeap
                                        << " globalUboBda=0x" << std::hex << mapped->globalUboBda
                                        << " objectBda=0x" << mapped->objectStorageBda
                                        << " materialBda=0x" << mapped->materialStorageBda
                                        << " boneBda=0x" << mapped->boneStorageBda << std::dec
                                        << " texBase=" << mapped->textureTableBase
                                        << " defaultSampler=" << mapped->defaultSampler
                                        << " textureTableCount=" << mapped->textureTableCount
                                        << '\n';
                            }
                            sceneVerifyOnce = true;
                          }
                        }
                    }
                    if (objectBuffer && totalSubMeshCount > 0) {
                        const auto* objs = reinterpret_cast<const ObjectData*>(
                            objectBuffer->getMappedMemory());
                        if (objs != nullptr) {
                            gfxPushG.meshMaterialId = maxMaterialIndex;
                        }
                    }
                    if constexpr (kMinimalRenderPath) {
                        world.each([&](const MeshComponent& meshComp) {
                            if (!meshComp.model || !meshComp.model->gpuDataReady) {
                                return;
                            }
                            gfxPushG.meshPosBda =
                                deviceAddressBits(meshComp.model->getPosBufferAddress());
                            gfxPushG.meshAttrBda =
                                deviceAddressBits(meshComp.model->getAttrBufferAddress());
                            const uint64_t indexBase =
                                deviceAddressBits(meshComp.model->getIndexBufferAddress());
                            gfxPushG.meshIdxBda =
                                indexBase +
                                static_cast<uint64_t>(gfxPushG.meshIndexBase) * sizeof(uint32_t);
                        });
                        static bool drawLogOnce = false;
                        if (!drawLogOnce) {
                            std::cerr << "[Minimal] draw sceneBda=0x" << std::hex
                                      << gfxPushG.sceneAddressesBda << " meshPosBda=0x"
                                      << gfxPushG.meshPosBda << std::dec
                                      << " gfxDebug=0x" << std::hex << gfxPushG.gfxDebug
                                      << std::dec << " verts=" << gfxPushG.meshVertexCount
                                      << '\n';
                            drawLogOnce = true;
                        }
                    }
                    if (kMinimalRenderPath && kMinimalDebugStage != 1) {
                        // Single depth+color pass (LESS) — enough for debug/minimal pipeline.
                        geometryRenderSystem->renderEntities(frameInfo, bindless_, gfxPushG,
                                                               totalSubMeshCount, false, (uint32_t)rs.vrsMode, false);
                    } else {
                        GfxHeapPush gfxPushZ = makeGfxPushForDraw(
                            slots(), frameIndex, device_, objectBuffer.get(), totalSubMeshCount);
                        gfxPushZ.meshProjection = meshProjRow;
                        gfxPushZ.meshView = meshViewRow;
                        gfxPushZ.sceneAddressesBda = gfxPushG.sceneAddressesBda;
                        if (objectBuffer && totalSubMeshCount > 0) {
                            const auto* objs = reinterpret_cast<const ObjectData*>(
                                objectBuffer->getMappedMemory());
                            if (objs != nullptr) {
                                gfxPushZ.meshMaterialId = maxMaterialIndex;
                            }
                        }
                        geometryRenderSystem->renderEntities(frameInfo, bindless_, gfxPushZ,
                                                           totalSubMeshCount, false, (uint32_t)rs.vrsMode, true);
                        geometryRenderSystem->renderEntities(frameInfo, bindless_, gfxPushG,
                                                           totalSubMeshCount, false, (uint32_t)rs.vrsMode, false);
                    }
                    {
                        uint32_t idx = 0;
                        world.each([&](PortalComponent& portal, TransformComponent& transform) {
                            if (idx >= Engine::kMaxPortals) return;
                            PortalGfxPush portalPush{}; portalPush.globalUbo = slots().globalUbo[frameIndex % 3]; portalRenderSystem->drawMask(cmd, bindless_, portalPush,
                                                         transform.transform.matrix, idx + 1);
                            idx++;
                        });
                    }

                    {
                        uint32_t portalCounter = 0;
                        world.each([&](PortalComponent& portal, TransformComponent& transform) {
                            if (portalCounter >= Engine::kMaxPortals) return;
                        
                            if (!portal.targetPortal.is_alive()) { portalCounter++; return; }
                        
                            PortalGfxPush portalPush{}; portalPush.globalUbo = slots().globalUbo[frameIndex % 3]; portalRenderSystem->drawDepthReset(cmd, bindless_, portalPush,
                                                               transform.transform.matrix, portalCounter + 1);
                            
                            auto& targetTransform = portal.targetPortal.get_mut<TransformComponent>();
                            float4x4 realCamWorld = MatrixInverse(camera.GetViewMatrix());
                            float4x4 mIn  = transform.transform.matrix;
                            float4x4 mOut = targetTransform.transform.matrix;
                            auto clean = [](float4x4 m) {
                                m._11 = Normalize(float3{m._11, m._12, m._13}).x; m._12 = Normalize(float3{m._11, m._12, m._13}).y; m._13 = Normalize(float3{m._11, m._12, m._13}).z;
                                m._21 = Normalize(float3{m._21, m._22, m._23}).x; m._22 = Normalize(float3{m._21, m._22, m._23}).y; m._23 = Normalize(float3{m._21, m._22, m._23}).z;
                                m._31 = Normalize(float3{m._31, m._32, m._33}).x; m._32 = Normalize(float3{m._31, m._32, m._33}).y; m._33 = Normalize(float3{m._31, m._32, m._33}).z;
                                return m;
                            };
                            static const float4x4 rot180 = MatrixRotationY(3.14159265f);
                            float4x4 portalTransition = MatrixMultiply(MatrixMultiply(clean(mOut), rot180), MatrixInverse(clean(mIn)));
                            float4x4 virtualCamWorld  = portalTransition * realCamWorld;
                        
                            GlobalUbo portalUbo = ubo;
                            float4x4 portalProjRow = portalUbo.projection;
                            float4x4 portalViewRow = MatrixInverse(virtualCamWorld);
                            portalUbo.camPos = GetMatrixPosition(virtualCamWorld);
                            portalUbo.portalID = portalCounter + 1;

                            float3 exitPortalPos = GetMatrixPosition(mOut);
                            float3 exitPortalNormal =
                                -Normalize(float3{mOut._31, mOut._32, mOut._33});

                            portalProjRow = computeObliqueProjection(
                                portalProjRow, portalViewRow, exitPortalPos, exitPortalNormal);

                            portalUbo.projection =
                                kGpuRowVectorMul ? portalProjRow : toGpuMatrix(portalProjRow);
                            portalUbo.view =
                                kGpuRowVectorMul ? portalViewRow : toGpuMatrix(portalViewRow);
                            portalUbo.invViewProj =
                                gpuInvViewProjection(portalViewRow, portalProjRow);
                            
                            portalUboBuffers[portalCounter][frameIndex]->writeToBuffer(&portalUbo);
                            portalUboBuffers[portalCounter][frameIndex]->flush();

                            // Also write to the big SSBO for the compute lighting pass
                            if(portalCounter < Engine::kMaxPortals) mappedPortalUbos[portalCounter] = portalUbo;
                        
                            FrameInfo portalFrameInfo = frameInfo;
                            portalFrameInfo.globalUboHeap =
                                portalFrameUboHeap_[portalCounter][frameIndex];

                            vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, portalCounter + 1);
                            GfxHeapPush portalGfxZ = gfxPushG;
                            GfxHeapPush portalGfxG = gfxPushG;
                            {
                                SceneAddresses portalScene{};
                                portalScene.globalUboHeap =
                                    portalFrameUboHeap_[portalCounter][frameIndex];
                                portalScene.objectStorageBda = deviceAddressBits(objectBuffer->deviceAddress());
                                if (materialBuffer) {
                                    portalScene.materialStorageBda =
                                        deviceAddressBits(materialBuffer->deviceAddress());
                                }
                                if (boneMatricesBuffer) {
                                    portalScene.boneStorageBda =
                                        deviceAddressBits(boneMatricesBuffer->deviceAddress());
                                }
                                portalScene.textureTableBase = slots().textureTableBase;
                                portalScene.defaultSampler = slots().defaultSampler;
                                portalScene.textureTableCount = bindless_.textures().count();
                                sceneAddressesBuffer->writeToBuffer(&portalScene);
                                sceneAddressesBuffer->flush();
                                const uint64_t portalSceneBda =
                                    deviceAddressBits(sceneAddressesBuffer->deviceAddress());
                                portalGfxZ.sceneAddressesBda = portalSceneBda;
                                portalGfxG.sceneAddressesBda = portalSceneBda;
                                portalGfxZ.globalUboHeap = portalScene.globalUboHeap;
                                portalGfxG.globalUboHeap = portalScene.globalUboHeap;
                                portalGfxZ.defaultSampler = portalScene.defaultSampler;
                                portalGfxG.defaultSampler = portalScene.defaultSampler;
                                portalGfxZ.textureTableCount = portalScene.textureTableCount;
                                portalGfxG.textureTableCount = portalScene.textureTableCount;
                                portalGfxZ.materialStorageBda = portalScene.materialStorageBda;
                                portalGfxG.materialStorageBda = portalScene.materialStorageBda;
                            }
                            portalGfxZ.meshProjection = portalProjRow;
                            portalGfxZ.meshView = portalViewRow;
                            portalGfxG.meshProjection = portalProjRow;
                            portalGfxG.meshView = portalViewRow;
                            geometryRenderSystem->renderEntities(portalFrameInfo, bindless_, portalGfxZ,
                                                               totalSubMeshCount, true, (uint32_t)rs.vrsMode, true);
                            geometryRenderSystem->renderEntities(portalFrameInfo, bindless_, portalGfxG,
                                                               totalSubMeshCount, true, (uint32_t)rs.vrsMode, false);
                        });
                    }

                    vkCmdEndRendering(cmd); });

                renderPipeline.addPass("Resolve G-Buffer", {
                    FrameGraph::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getEmissive()->getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getPortalID()->getImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                    FrameGraph::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                }, [](VkCommandBuffer cmd) {});

                if (!kMinimalRenderPath) {
                renderPipeline.addPass("VSM Mark Pages", {}, [&](VkCommandBuffer cmd)
                                       {
                    if (csmNeedsFullUpdate) {
                        // Очистка таблиц перед анализом только при полном обновлении
                        vkCmdFillBuffer(cmd, shadowSystem->getVSM()->getPageTable()->getBuffer(), 0, VK_WHOLE_SIZE, 0xFFFFFFFF);
                        vkCmdFillBuffer(cmd, shadowSystem->getVSM()->getAllocator()->getBuffer(), 0, 4, 0); 

                        VkBufferMemoryBarrier2 barriers[2]{};
                        barriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                        barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                        barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                        barriers[0].buffer = shadowSystem->getVSM()->getPageTable()->getBuffer();
                        barriers[0].size = VK_WHOLE_SIZE;

                        barriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                        barriers[1].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                        barriers[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                        barriers[1].dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                        barriers[1].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
                        barriers[1].buffer = shadowSystem->getVSM()->getAllocator()->getBuffer();
                        barriers[1].size = VK_WHOLE_SIZE;

                        VkDependencyInfo depInfo{};
                        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        depInfo.bufferMemoryBarrierCount = 2;
                        depInfo.pBufferMemoryBarriers = barriers;
                        vkCmdPipelineBarrier2(cmd, &depInfo);
                    }

                    vsmMarkPagesShader->bind(cmd);
                    {
                        VsmMarkHeapPC vsmPush = makeVsmMarkPush(slots(), frameIndex);
                        vsmMarkPagesShader->pushConstants(cmd, bindless_, &vsmPush, sizeof(vsmPush));
                    }
                    vsmMarkPagesShader->dispatch(cmd, 4, 1, 1); 
                    
                    // КРИТИЧЕСКИ ВАЖНО: Барьер, чтобы Compute Lighting увидел записанные страницы!
                    VkBufferMemoryBarrier2 vsmBarrier{};
                    vsmBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                    vsmBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    vsmBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
                    vsmBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    vsmBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
                    vsmBarrier.buffer = shadowSystem->getVSM()->getPageTable()->getBuffer();
                    vsmBarrier.size = VK_WHOLE_SIZE;
                    
                    VkDependencyInfo depInfo{};
                    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depInfo.bufferMemoryBarrierCount = 1;
                    depInfo.pBufferMemoryBarriers = &vsmBarrier;
                    vkCmdPipelineBarrier2(cmd, &depInfo); });

                                renderPipeline.addPass("VSM Geometry Render", {FrameGraph::createImageBarrier(shadowSystem->getVSM()->getPhysicalAtlas()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1)}, [&](VkCommandBuffer cmd)
                                       {
                    VkRenderingAttachmentInfo depthAttachment{};
                    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    depthAttachment.imageView = shadowSystem->getVSM()->getPhysicalAtlas()->getImageView();
                    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    VkRenderingInfo renderingInfo{};
                    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                    renderingInfo.renderArea.extent = { 4096, 4096 };
                    renderingInfo.layerCount = 1;
                    renderingInfo.pDepthAttachment = &depthAttachment;
                         renderingInfo.pStencilAttachment = &depthAttachment;
                    vkCmdBeginRendering(cmd, &renderingInfo);

                    VkClearAttachment clearAttachment{};
                    clearAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                    clearAttachment.clearValue.depthStencil = { 1.0f, 0 };

                    if (csmNeedsFullUpdate) {
                        VkClearRect clearRect{};
                        clearRect.rect.offset = { 0, 0 };
                        clearRect.rect.extent = { 4096, 4096 };
                        clearRect.baseArrayLayer = 0; clearRect.layerCount = 1;
                        vkCmdClearAttachments(cmd, 1, &clearAttachment, 1, &clearRect);

                        world.each([&](LightComponent& lightComp, TransformComponent& transComp) {
                            auto& light = lightComp.light;
                            auto& trans = transComp.transform;
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
                                    ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, light.lightSpaceMatrix, world, false);
                                } else if (light.type == LightType::Point) {
                                    float3 pos = trans.position;
                                    const float3 dirs[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
                                    const float3 ups[6]  = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};  
                                    for (int face = 0; face < 6; face++) {
                                        int fx = pxX + face * tileSize; int fy = pxY;
                                        if (fx + tileSize > 4096) { fx = fx % 4096; fy += tileSize; }
                                        VkViewport vp{}; vp.x = fx; vp.y = fy; vp.width = tileSize; vp.height = tileSize; vp.maxDepth = 1.0f;
                                        vkCmdSetViewport(cmd, 0, 1, &vp);
                                        VkRect2D sc{ {fx, fy}, {(uint32_t)tileSize, (uint32_t)tileSize} };
                                        vkCmdSetScissor(cmd, 0, 1, &sc);
                                        // ФИКС: Используем shadowPerspective для переворота Y-оси!
                                        float4x4 faceProj = shadowPerspective(90.0f, 1.0f, 0.1f, light.radius);
                                        float4x4 faceView = MatrixLookAtLH(pos, pos + dirs[face], ups[face]);
                                        float4x4 faceMatrix = faceProj * faceView;
                                            ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, faceMatrix, world, false);
                                    }
                                }
                            }
                        });
                    } else if (hasDirtyRegion) {
                        world.each([&](LightComponent& lightComp, TransformComponent& transComp) {
                            auto& light = lightComp.light;
                            auto& trans = transComp.transform;
                            if (light.enable && light.castShadows && light.shadowSlot >= 0 && (light.type == LightType::Spot || light.type == LightType::Point)) {
                                int encodedInt = light.shadowSlot;
                                int realSlot = encodedInt / 10000;
                                int tileSize = encodedInt % 10000;
                                int pxX = (realSlot % 32) * 128;
                                int pxY = (realSlot / 32) * 128;

                                float4x4 lightMatrix = light.lightSpaceMatrix;
                                float3 corners[8] = {
                                    {dirtyMin.x, dirtyMin.y, dirtyMin.z}, {dirtyMax.x, dirtyMin.y, dirtyMin.z},
                                    {dirtyMin.x, dirtyMax.y, dirtyMin.z}, {dirtyMax.x, dirtyMax.y, dirtyMin.z},
                                    {dirtyMin.x, dirtyMin.y, dirtyMax.z}, {dirtyMax.x, dirtyMin.y, dirtyMax.z},
                                    {dirtyMin.x, dirtyMax.y, dirtyMax.z}, {dirtyMax.x, dirtyMax.y, dirtyMax.z}
                                };
                                float minX = 1.0f, minY = 1.0f, maxX = 0.0f, maxY = 0.0f;
                                for(int k=0; k<8; k++) {
                                    float4 pt = lightMatrix * MakeFloat4(corners[k], 1.0f);
                                    float3 ndc = MakeFloat3(pt) / pt.w;
                                    float2 uv = MakeFloat2(ndc) * 0.5f + float2{0.5f, 0.5f};
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
                                        ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, light.lightSpaceMatrix, world, false);
                                    } else if (light.type == LightType::Point) {
                                        float3 pos = trans.position;
                                        const float3 dirs[6] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
                                        const float3 ups[6]  = {{0,-1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}, {0,-1,0}, {0,-1,0}};  
                                        for (int face = 0; face < 6; face++) {
                                            int fx = pxX + face * tileSize; int fy = pxY;
                                            if (fx + tileSize > 4096) { fx = fx % 4096; fy += tileSize; }
                                            vp.x = fx; vp.y = fy; vkCmdSetViewport(cmd, 0, 1, &vp);
                                            sc.offset = { fx + localPxX, fy + localPxY }; vkCmdSetScissor(cmd, 0, 1, &sc);
                                            // ФИКС: Используем shadowPerspective!
                                            float4x4 faceProj = shadowPerspective(90.0f, 1.0f, 0.1f, light.radius);
                                            float4x4 faceView = MatrixLookAtLH(pos, pos + dirs[face], ups[face]);
                                            float4x4 faceMatrix = faceProj * faceView;
                                            ShadowGfxPush shPush{}; shPush.objectStorage = slots().objectStorage; shPush.boneStorage = slots().boneStorage; shadowRenderSystem->renderShadow(cmd, bindless_, shPush, faceMatrix, world, false);
                                        }
                                    }
                                }
                            }
                        });
                    }
                    vkCmdEndRendering(cmd); });
                } // !kMinimalRenderPath (VSM)

                if (!kMinimalRenderPath) {
                renderPipeline.addPass("Hi-Z Pass", {}, [&](VkCommandBuffer cmd)
                                       { hizSystem->dispatch(cmd, bindless_, slots().gbufferDepth, slots().hiZ, extent); });
                renderPipeline.addPass("GTAO Pass", {FrameGraph::createImageBarrier(gtaoOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
      GtaoHeapPC pc = makeGtaoPush(slots(), frameIndex);
      gtaoSystem->compute(cmd, bindless_, pc, std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)); });
                renderPipeline.addPass("Radiance Cascades GI", {},
                                       [&](VkCommandBuffer cmd)
                                       {
                    float4 gridMin{-8.0f, -8.0f, -8.0f, 0.0f};
                    float4 gridMax{8.0f, 8.0f, 8.0f, 0.0f};
                    CascadeMergePC mergePc = makeCascadeMergePush(
                        slots(), gridMin, gridMax,
                        RCConfig::PROBE_X, RCConfig::PROBE_Y, RCConfig::PROBE_Z, RCConfig::OCTA_SIZE);
                    GiSampleHeapPC samplePc = makeGiSamplePush(
                        slots(), frameIndex, gridMin, gridMax,
                        RCConfig::PROBE_X, RCConfig::PROBE_Y, RCConfig::PROBE_Z, RCConfig::OCTA_SIZE);
                    rcSystem->dispatch(cmd, bindless_, mergePc, samplePc, extent); });

                renderPipeline.addPass("Probe Update", {}, [&](VkCommandBuffer cmd)
                                       { world.each([&](flecs::entity e, TransformComponent &t, ReflectionProbeComponent &p) {
                        if (p.updateNeeded && p.textureIndex != -1) {
                            p.updateNeeded = false;
                            rtReflectionSystem->probeRenderShader->bind(cmd);
                            ProbeRenderHeapPC probePc{};
                            const auto& sl = slots();
                            probePc.globalUbo = sl.globalUbo[frameIndex % 3];
                            probePc.giCascade0 = sl.giCascade0 ? sl.giCascade0 : sl.giDiffuse;
                            probePc.outProbeTex = static_cast<uint32_t>(p.textureIndex);
                            probePc.rtTlas = sl.rtTlas;
                            probePc.objectStorage = sl.objectStorage;
                            probePc.materialStorage = sl.materialStorage;
                            probePc.defaultSampler = sl.defaultSampler;
                            probePc.probePos = MakeFloat4(t.transform.position, 1.0f);
                            probePc.resolution = static_cast<int32_t>(p.resolution);
                            rtReflectionSystem->probeRenderShader->pushConstants(cmd, bindless_, &probePc, sizeof(probePc));
                            rtReflectionSystem->probeRenderShader->dispatch(cmd, (p.resolution + 7) / 8, (p.resolution + 7) / 8, 1);
                        } }); });

                renderPipeline.addPass("RT Reflections", {FrameGraph::createImageBarrier(rtReflectionSystem->rtReflectionsTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    if (rs.enableRTReflections && tlasHandle != VK_NULL_HANDLE) {
                        rtReflectionSystem->rtReflectionsShader->bind(cmd);
                        RtReflectionPush rtPush = makeRtPush(slots(), frameIndex);
                        rtReflectionSystem->rtReflectionsShader->pushConstants(cmd, bindless_, &rtPush, sizeof(rtPush));
                        rtReflectionSystem->rtReflectionsShader->dispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
                    } });
                    
                renderPipeline.addPass("Volumetric Fog Pass", {
                    FrameGraph::createImageBarrier(shadowSystem->getVSM()->getPhysicalAtlas()->getImage(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 1),
                    FrameGraph::createImageBarrier(volumetricSystem->volumetricTex->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)
                }, [&](VkCommandBuffer cmd) {
                    volumetricSystem->shader->bind(cmd);
                    VolumetricPush volPush = makeVolumetricPush(slots(), frameIndex);
                    volumetricSystem->shader->pushConstants(cmd, bindless_, &volPush, sizeof(volPush));
                    uint32_t vw = std::max(1u, extent.width / 2);
                    uint32_t vh = std::max(1u, extent.height / 2);
                    volumetricSystem->shader->dispatch(cmd, (vw + 15)/16, (vh + 15)/16, 1);
                });
                } // !kMinimalRenderPath (Hi-Z / GTAO / GI / RT / volumetric)

                if ((kMinimalRenderPath && kMinimalUseDeferredLighting && !kMinimalBlitGBufferAlbedo) ||
                    !kMinimalRenderPath) {
                    renderPipeline.addPass("Compute Lighting", {
                        FrameGraph::createImageBarrier(gBuffer->getNormalRoughness()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                        FrameGraph::createImageBarrier(gBuffer->getAlbedoMetallic()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                        FrameGraph::createImageBarrier(gBuffer->getHeightAO()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                        FrameGraph::createImageBarrier(gBuffer->getEmissive()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT),
                        FrameGraph::createImageBarrier(gBuffer->getDepth()->getImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT),
                        FrameGraph::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)
                    }, [&](VkCommandBuffer cmd) {
                        const uint64_t sceneBda =
                            deviceAddressBits(sceneAddressesBuffer->deviceAddress());
                        LightingHeapPC lightPc = makeLightingPush(slots(), frameIndex, sceneBda);
                        lightingSystem->computeLighting(cmd, bindless_, lightPc, extent.width, extent.height);
                    });
                }

                if (!kMinimalRenderPath) {
                renderPipeline.addPass("SSGI Pass", {FrameGraph::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT), FrameGraph::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
      SsgiHeapPC spc = makeSsgiPush(slots(), frameIndex);
      ssgiSystem->computeSSGI(cmd, bindless_, spc, extent.width, extent.height); });
                renderPipeline.addPass("SSGI Denoise Pass", {FrameGraph::createImageBarrier(ssgiRawTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT)}, [&](VkCommandBuffer cmd)
                                       {
      SsgiDenoiseHeapPC spc = makeSsgiDenoisePush(slots(), frameIndex);
      ssgiSystem->computeDenoise(cmd, bindless_, spc, extent.width, extent.height); });
                renderPipeline.addPass("Post Processing Pass", {FrameGraph::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT), FrameGraph::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
            postProcessShader->bind(cmd);
            PostProcessPush ppPush = makePostPush(slots(), frameIndex);
            postProcessShader->pushConstants(cmd, bindless_, &ppPush, sizeof(ppPush));
            postProcessShader->dispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1); });
                
                renderPipeline.addPass("TAA History Update", {FrameGraph::createImageBarrier(taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT), FrameGraph::createImageBarrier(taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT)}, [&](VkCommandBuffer cmd)
                                       {
                    VkImageBlit blit{};
                    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.srcOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }; blit.dstOffsets[1] = { (int32_t)extent.width, (int32_t)extent.height, 1 };
                    vkCmdBlitImage(cmd, taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
                });
                } // !kMinimalRenderPath (SSGI / post / TAA)
                } // runScenePasses

                VkImage swapChainImage = renderer_.getCurrentSwapChainImage();
                VkImage blitSourceImage = postProcessTexture->getImage();
                if (kMinimalRenderPath) {
                    if (kMinimalDebugStage == 4) {
                        blitSourceImage = gBuffer->getNormalRoughness()->getImage();
                    } else if (kMinimalDebugStage == 3) {
                        blitSourceImage = gBuffer->getEmissive()->getImage();
                    } else if (kMinimalBlitGBufferAlbedo) {
                        blitSourceImage = gBuffer->getAlbedoMetallic()->getImage();
                    } else {
                        blitSourceImage = hdrOutputTexture->getImage();
                    }
                } else {
                    blitSourceImage = postProcessTexture->getImage();
                }
                std::vector<VkImageMemoryBarrier2> blitBarriers;
                if (kMinimalRenderPath && kMinimalDebugStage != 1 &&
                    blitSourceImage != hdrOutputTexture->getImage()) {
                    blitBarriers.push_back(FrameGraph::createImageBarrier(
                        blitSourceImage,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                        VK_ACCESS_2_TRANSFER_READ_BIT));
                } else if (!kMinimalRenderPath || blitSourceImage == hdrOutputTexture->getImage()) {
                    blitBarriers.push_back(FrameGraph::createImageBarrier(
                        blitSourceImage, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT));
                }
                if (kMinimalRenderPath && kMinimalDebugStage == 1) {
                    renderPipeline.addPass("Debug Present Cyan", {}, [&](VkCommandBuffer cmd) {
                        static int lastStage = -1;
                        if (lastStage != kMinimalDebugStage) {
                            std::cerr << "[Debug] Present path: swapchain CYAN clear (stage 1, no G-Buffer)\n";
                            lastStage = kMinimalDebugStage;
                        }
                        debugPresentSolidColor(cmd, swapChainImage, VkClearColorValue{{0.f, 1.f, 1.f, 1.f}});
                    });
                } else if (kMinimalRenderPath && kMinimalSkipBlitPresent) {
                    renderPipeline.addPass("Present Skip Blit", {}, [&](VkCommandBuffer cmd) {
                        static bool logged = false;
                        if (!logged) {
                            std::cerr << "[Debug] Present path: skip blit (black clear, mesh isolation)\n";
                            logged = true;
                        }
                        debugPresentSolidColor(cmd, swapChainImage, VkClearColorValue{{0.f, 0.f, 0.f, 1.f}});
                    });
                } else if (kMinimalRenderPath && kMinimalBlitToSwapchainPresent) {
                    renderPipeline.addPass("Blit Scene Present", blitBarriers, [&](VkCommandBuffer cmd) {
                        static int lastStage = -1;
                        if (lastStage != kMinimalDebugStage) {
                            std::cerr << "[Debug] Present path: blit G-Buffer albedo (stage "
                                      << kMinimalDebugStage << ")\n";
                            lastStage = kMinimalDebugStage;
                        }
                        VkImageMemoryBarrier2 swapToDst = FrameGraph::createImageBarrier(
                            swapChainImage,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            0,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
                        VkDependencyInfo depInfoDst{};
                        depInfoDst.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        depInfoDst.imageMemoryBarrierCount = 1;
                        depInfoDst.pImageMemoryBarriers = &swapToDst;
                        vkCmdPipelineBarrier2(cmd, &depInfoDst);
                        VkImageBlit blit{};
                        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.srcOffsets[1] = {(int32_t)extent.width, (int32_t)extent.height, 1};
                        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.dstOffsets[1] = {(int32_t)extent.width, (int32_t)extent.height, 1};
                        vkCmdBlitImage(
                            cmd,
                            blitSourceImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            swapChainImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &blit,
                            VK_FILTER_LINEAR);
                        transitionSwapchainToPresent(cmd, swapChainImage);
                    });
                } else if (kMinimalRenderPath && kMinimalUseImGuiSceneViewport) {
                    renderPipeline.addPass("ImGui Scene Viewport", {}, [&](VkCommandBuffer cmd) {
                        uiManager->setSceneViewTexture(gBuffer->getAlbedoMetallic());
                        VkImageMemoryBarrier2 swapToColor = FrameGraph::createImageBarrier(
                            swapChainImage,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            0,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                        VkDependencyInfo dep{};
                        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        dep.imageMemoryBarrierCount = 1;
                        dep.pImageMemoryBarriers = &swapToColor;
                        vkCmdPipelineBarrier2(cmd, &dep);
                        uiManager->UpdateUI(window_, camera, cmd);
                        renderer_.beginSwapChainRendering(cmd);
                        uiManager->RenderUI(cmd);
                        renderer_.endSwapChainRendering(cmd);
                    });
                } else {
                    renderPipeline.addPass("Blit and UI", blitBarriers, [&](VkCommandBuffer cmd) {
                        VkImageMemoryBarrier2 swapToDst = FrameGraph::createImageBarrier(
                            swapChainImage,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            0,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT);
                        VkDependencyInfo depInfoDst{};
                        depInfoDst.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        depInfoDst.imageMemoryBarrierCount = 1;
                        depInfoDst.pImageMemoryBarriers = &swapToDst;
                        vkCmdPipelineBarrier2(cmd, &depInfoDst);
                        VkImageBlit blit{};
                        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.srcOffsets[1] = {(int32_t)extent.width, (int32_t)extent.height, 1};
                        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.dstOffsets[1] = {(int32_t)extent.width, (int32_t)extent.height, 1};
                        vkCmdBlitImage(
                            cmd,
                            blitSourceImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            swapChainImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            1,
                            &blit,
                            VK_FILTER_LINEAR);
                        VkImageMemoryBarrier2 swapToAttach = FrameGraph::createImageBarrier(
                            swapChainImage,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                            VK_ACCESS_2_TRANSFER_WRITE_BIT,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                        VkDependencyInfo depInfoAttach{};
                        depInfoAttach.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        depInfoAttach.imageMemoryBarrierCount = 1;
                        depInfoAttach.pImageMemoryBarriers = &swapToAttach;
                        vkCmdPipelineBarrier2(cmd, &depInfoAttach);
                        uiManager->UpdateUI(window_, camera, cmd);
                        renderer_.beginSwapChainRendering(cmd);
                        uiManager->RenderUI(cmd);
                        renderer_.endSwapChainRendering(cmd);
                    });
                } // debug present vs blit paths
                if (kMinimalRenderPath && kMinimalDebugStage != 1 && kMinimalBlitGBufferAlbedo &&
                    !kMinimalSkipBlitPresent) {
                    renderPipeline.addPass("Reset Layouts", {
                        FrameGraph::createImageBarrier(
                            blitSourceImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_READ_BIT)
                    }, [](VkCommandBuffer) {});
                } else if (kMinimalRenderPath) {
                    renderPipeline.addPass("Reset Layouts", {
                        FrameGraph::createImageBarrier(hdrOutputTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)
                    }, [](VkCommandBuffer) {});
                } else {
                    renderPipeline.addPass("Reset Layouts", {FrameGraph::createImageBarrier(postProcessTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT), FrameGraph::createImageBarrier(taaHistoryTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT), FrameGraph::createImageBarrier(taaResolvedTexture->getImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT)}, [](VkCommandBuffer) {});
                }
                renderPipeline.execute(commandBuffer);
                renderer_.endFrame();
                frameCount++;
            }
        }
        vkDeviceWaitIdle(device_.device());
}

} // namespace burnhope
