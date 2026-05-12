#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <filesystem>
#include <unordered_map>
#include "../Render/Material.hpp"
#include "../Render/Texture.hpp"
#include "../Render/ComputeShader.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace burnhope {
    struct PreviewRenderer {
        BurnhopeDevice& device;
        std::unique_ptr<BurnhopeDescriptorPool> pool;
        std::unique_ptr<BurnhopeDescriptorSetLayout> layout;
        std::unique_ptr<BurnhopeTexture> outputTex;
        std::shared_ptr<BurnhopeTexture> defaultWhite;
        std::shared_ptr<BurnhopeTexture> defaultNormal;
        std::shared_ptr<BurnhopeTexture> hdrTex;
        std::unique_ptr<ComputeShader> shader;
        VkDescriptorSet descSet = VK_NULL_HANDLE;
        VkDescriptorSet imguiSet = VK_NULL_HANDLE;
        
        std::shared_ptr<BurnhopeTexture> lastAA;
        std::shared_ptr<BurnhopeTexture> lastORMX;
        std::shared_ptr<BurnhopeTexture> lastNorm;

        struct PushConsts {
            glm::vec4 albedoColor;
            glm::vec4 emissiveColor;
            glm::vec4 matParams;
            glm::vec4 uvScale_triSc;
            glm::vec4 camPos_time;
            glm::ivec4 flags;
        };

        PreviewRenderer(BurnhopeDevice& dev) : device(dev) {
            pool = BurnhopeDescriptorPool::Builder(device).setMaxSets(10).setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1).addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9).build();

            layout = BurnhopeDescriptorSetLayout::Builder(device)
                .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
                .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT).build();

            outputTex = std::make_unique<BurnhopeTexture>(device, VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{256, 256, 1}, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
            outputTex->transitionLayout(device.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

            std::vector<VkDescriptorSetLayout> layouts = {layout->getDescriptorSetLayout()};
            shader = std::make_unique<ComputeShader>(device, "shaders/mat_preview.comp.spv", layouts, sizeof(PushConsts));

            defaultWhite = BurnhopeTexture::createTextureFromFile(device, "../textures/white.png");
            defaultNormal = BurnhopeTexture::createTextureFromFile(device, "../textures/white.png");
            
            if (std::filesystem::exists("../textures/hdr.jpg")) {
                hdrTex = BurnhopeTexture::createTextureFromFile(device, "../textures/hdr.jpg");
            } else {
                hdrTex = defaultWhite;
            }

            pool->allocateDescriptor(layout->getDescriptorSetLayout(), descSet);
            imguiSet = ImGui_ImplVulkan_AddTexture(outputTex->getSampler(), outputTex->getImageView(), VK_IMAGE_LAYOUT_GENERAL);
        }

        ~PreviewRenderer() { if (imguiSet) ImGui_ImplVulkan_RemoveTexture(imguiSet); }

        void UpdateAndDispatch(VkCommandBuffer cmd, std::shared_ptr<Material> mat, int shape, const glm::vec3& cPos, float time) {
            auto aaTex = mat->packedAlbedoAlpha ? mat->packedAlbedoAlpha : (mat->albedoMap ? mat->albedoMap : defaultWhite);
            auto ormxTex = mat->packedORMX ? mat->packedORMX : (mat->ormMap ? mat->ormMap : defaultWhite);
            auto normTex = mat->packedNormal ? mat->packedNormal : (mat->normalMap ? mat->normalMap : defaultNormal);

            if (aaTex != lastAA || ormxTex != lastORMX || normTex != lastNorm) {
                vkDeviceWaitIdle(device.device());

                VkDescriptorImageInfo outInfo = outputTex->getImageInfo(); outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                VkDescriptorImageInfo aaInfo = aaTex->getImageInfo();
                VkDescriptorImageInfo ormxInfo = ormxTex->getImageInfo();
                VkDescriptorImageInfo normPackedInfo = normTex->getImageInfo();
                VkDescriptorImageInfo hdrInfo = hdrTex ? hdrTex->getImageInfo() : defaultWhite->getImageInfo();

                BurnhopeDescriptorWriter(*layout, *pool)
                    .writeImage(0, &outInfo)
                    .writeImage(1, &aaInfo)
                    .writeImage(2, &ormxInfo)
                    .writeImage(3, &normPackedInfo)
                    .writeImage(4, &hdrInfo)
                    .overwrite(descSet);

                lastAA = aaTex; lastORMX = ormxTex; lastNorm = normTex;
            }

            shader->bind(cmd);
            shader->bindDescriptorSets(cmd, {descSet});
            
            int bitmask = 0;
            if (mat->packedAlbedoAlpha || mat->hasAlbedo) bitmask |= 1;
            if (mat->packedORMX || mat->hasORM || mat->hasMetallic || mat->hasRoughness || mat->hasAO) bitmask |= 2;
            if (mat->packedNormal || mat->hasNormal) bitmask |= 4;
            if (mat->isTransparent) bitmask |= 8;
            if (mat->useTriplanar) bitmask |= 16;
            if (mat->repeatTexture) bitmask |= 32;

            PushConsts pc{};
            pc.albedoColor = mat->albedoColor;
            pc.emissiveColor = glm::vec4(mat->emissiveColor, mat->emissiveIntensity);
            pc.matParams = glm::vec4(mat->metallicStrength, mat->roughnessStrength, mat->normalStrength, 0.0f);
            pc.uvScale_triSc = glm::vec4(mat->uvScale.x, mat->uvScale.y, mat->triplanarScale, 0.0f);
            pc.camPos_time = glm::vec4(cPos, time);
            pc.flags = glm::ivec4(shape, bitmask, 0, 0);
            
            shader->pushConstants(cmd, &pc, sizeof(PushConsts));
            shader->dispatch(cmd, (256 + 15) / 16, (256 + 15) / 16, 1);
            
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = outputTex->getImage(); barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    };

    class MaterialEditorWindow : public IUIWindow {
    private:
        std::shared_ptr<Material> currentMat;
        std::string currentPath;
        std::unordered_map<std::string, VkDescriptorSet> thumbnailCache;
        
        std::unique_ptr<PreviewRenderer> previewRenderer;
        glm::vec2 previewAngles = glm::vec2(0.0f, 0.0f);
        float previewRadius = 4.0f;
        int previewShape = 0;

        void ClearThumbnailCache() {
            for (auto& kv : thumbnailCache) {
                ImGui_ImplVulkan_RemoveTexture(kv.second);
            }
            thumbnailCache.clear();
        }

        VkDescriptorSet GetTexID(UIContext& context, std::shared_ptr<BurnhopeTexture> tex, const std::string& path) {
            if (!tex || path.empty()) return VK_NULL_HANDLE;
            if (thumbnailCache.find(path) == thumbnailCache.end()) {
                thumbnailCache[path] = ImGui_ImplVulkan_AddTexture(tex->getSampler(), tex->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            }
            return thumbnailCache[path];
        }

        void DrawPBRPreview(UIContext& context) {
            if (!previewRenderer) return;
            
            static float time = 0.0f; time += ImGui::GetIO().DeltaTime;

            glm::vec3 camPos = glm::vec3(
                previewRadius * cos(previewAngles.y) * sin(previewAngles.x),
                previewRadius * sin(previewAngles.y),
                previewRadius * cos(previewAngles.y) * cos(previewAngles.x)
            );

            previewRenderer->UpdateAndDispatch(context.currentCommandBuffer, currentMat, previewShape, camPos, time);

            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)previewRenderer->imguiSet, ImVec2(256, 256));
            ImGui::SetCursorScreenPos(p);
            ImGui::InvisibleButton("##pbr_preview_btn", ImVec2(256, 256));
            
            if (ImGui::IsItemHovered()) {
                float scroll = ImGui::GetIO().MouseWheel;
                if (scroll != 0.0f) {
                    previewRadius -= scroll * 0.5f;
                    previewRadius = glm::clamp(previewRadius, 1.5f, 15.0f);
                }
            }

            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                previewAngles.x -= delta.x * 0.01f; // Инвертировано для правильного ощущения вращения объекта
                previewAngles.y += delta.y * 0.01f;
                previewAngles.y = glm::clamp(previewAngles.y, -1.5f, 1.5f);
            }

            ImGui::Spacing();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::Combo("##Shape", &previewShape, "Sphere\0Cube\0Plane\0");
            ImGui::SameLine();
            ImGui::TextDisabled("Drag: Rotate | Scroll: Zoom");
        }

        template<typename F>
        void DrawMaterialProperty(const char* label, std::shared_ptr<BurnhopeTexture> tex, const std::string& currentTexPath, 
                                  UIContext& context, float* sliderVal, float minV, float maxV, 
                                  float* colorVal, bool isColor, bool hasAlpha, bool hideSliderWithTex, 
                                  bool* optCheckbox, const char* optCheckboxLabel, bool& changed, F setter) {
            ImGui::PushID(label);
            ImGui::BeginGroup();

            VkDescriptorSet texID = GetTexID(context, tex, currentTexPath);
            if (texID) {
                if (ImGui::ImageButton(("##" + std::string(label)).c_str(), (ImTextureID)texID, ImVec2(56, 56))) ImGui::OpenPopup("SelectTexPopup");
            } else {
                ImVec4 bgColor = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
                if (isColor && colorVal) {
                    bgColor = ImVec4(colorVal[0], colorVal[1], colorVal[2], 1.0f);
                } else if (sliderVal) {
                    bgColor = ImVec4(*sliderVal, *sliderVal, *sliderVal, 1.0f);
                }
                ImGui::PushStyleColor(ImGuiCol_Button, bgColor);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(bgColor.x * 1.2f, bgColor.y * 1.2f, bgColor.z * 1.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, bgColor);
                if (ImGui::Button(("##Btn" + std::string(label)).c_str(), ImVec2(56, 56))) ImGui::OpenPopup("SelectTexPopup");
                ImGui::PopStyleColor(3);
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const char* path = (const char*)payload->Data;
                    std::filesystem::path p(path);
                    if (p.extension() == ".png" || p.extension() == ".jpg" || p.extension() == ".jpeg" || p.extension() == ".tga") {
                        setter(p.string());
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            
            // Правый блок (Текст и Ползунок/Цвет)
            ImGui::BeginGroup();
            
            float availWidth = ImGui::GetContentRegionAvail().x;
            ImGui::Text("%s", label);
            
            if (!currentTexPath.empty()) {
                ImGui::SameLine(availWidth - 24.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
                if (ImGui::Button("X", ImVec2(24, 18))) setter("");
                ImGui::PopStyleColor();
            }

            if (isColor && colorVal) {
                ImGui::SetNextItemWidth(availWidth);
                int flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoLabel;
                if (!hasAlpha) flags |= ImGuiColorEditFlags_NoAlpha;
                if (hasAlpha) changed |= ImGui::ColorEdit4("##col", colorVal, flags);
                else changed |= ImGui::ColorEdit3("##col", colorVal, flags);
            } 
            
            if (sliderVal) {
                if (!hideSliderWithTex || currentTexPath.empty()) {
                    ImGui::SetNextItemWidth(availWidth);
                    changed |= ImGui::SliderFloat("##val", sliderVal, minV, maxV, "%.2f");
                }
            }
            
            if (hideSliderWithTex && !currentTexPath.empty() && !isColor) {
                ImGui::TextDisabled("Driven by texture map");
            }
            if (optCheckbox && optCheckboxLabel) {
                changed |= ImGui::Checkbox(optCheckboxLabel, optCheckbox);
            }

            ImGui::EndGroup();
            ImGui::EndGroup();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::BeginPopup("SelectTexPopup")) {
                auto texs = context.GetProjectAssets({".png", ".jpg", ".jpeg", ".tga"});
                if (ImGui::Selectable("None")) setter("");
                for (const auto& t : texs) {
                    if (ImGui::Selectable(std::filesystem::path(t).filename().string().c_str())) setter(t);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        void ReloadMaterialInScene(UIContext& context, const std::string& path, std::shared_ptr<Material> newMat) {
            auto view = context.registry->view<MeshComponent>();
            for (auto entity : view) {
                auto& mc = view.get<MeshComponent>(entity);
                for (size_t i = 0; i < mc.materialPaths.size(); i++) {
                    if (mc.materialPaths[i] == path) {
                        if (mc.materials[i] && mc.materials[i] != newMat) {
                            context.safeDeleteQueue.push_back(mc.materials[i]);
                        }
                        mc.materials[i] = newMat;
                    }
                }
            }
        }

    public:
        MaterialEditorWindow() : IUIWindow("Material Editor") {}
        ~MaterialEditorWindow() { ClearThumbnailCache(); }

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);
            ImGui::PopStyleVar();

            if (!previewRenderer && context.currentCommandBuffer != VK_NULL_HANDLE) {
                previewRenderer = std::make_unique<PreviewRenderer>(*context.device);
            }

            std::string selectedPath = "";
            if (context.selectedAssets.size() == 1) {
                std::filesystem::path p(context.selectedAssets[0]);
                if (p.extension() == ".bhmat" || p.extension() == ".json") {
                    selectedPath = context.selectedAssets[0];
                }
            }

            if (selectedPath.empty()) {
                ImGui::TextDisabled("Select a .bhmat file in Content Browser");
                ImGui::End();
                return;
            }

            if (currentPath != selectedPath) {
                vkDeviceWaitIdle(context.device->device());
                ClearThumbnailCache();
                currentPath = selectedPath;
                if (currentMat) context.safeDeleteQueue.push_back(currentMat);
                currentMat = Material::loadFromJson(*context.device, currentPath);
                if (!currentMat) currentMat = std::make_shared<Material>();
            }

            if (!currentMat) {
                ImGui::End();
                return;
            }

            // Заголовок материала
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Editing: %s", std::filesystem::path(currentPath).filename().string().c_str());
            ImGui::Separator();
            ImGui::Spacing();
            bool changed = false;

            if (ImGui::BeginTable("MatEditorTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("PreviewCol", ImGuiTableColumnFlags_WidthFixed, 280.0f);
                ImGui::TableSetupColumn("PropsCol", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                
                ImGui::TableSetColumnIndex(0);
                
                // Враппер-Child блокирует случайный скролл родительского окна во время зума
                ImGui::BeginChild("PreviewChild", ImVec2(275, 310), false, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
                DrawPBRPreview(context);
                ImGui::EndChild();

                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Texturing & UV");
            if (currentMat->isPacking) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "  [ ⏳ Packing Textures Async... ]");
            }
                ImGui::Spacing();

                changed |= ImGui::Checkbox("Repeat", &currentMat->repeatTexture);
                ImGui::SameLine(100.0f);
                changed |= ImGui::Checkbox("Triplanar", &currentMat->useTriplanar);
                
                ImGui::SetNextItemWidth(260.0f);
                changed |= ImGui::DragFloat2("Scale##UV", &currentMat->uvScale.x, 0.01f);
                
                if (currentMat->useTriplanar) {
                    ImGui::SetNextItemWidth(260.0f);
                    changed |= ImGui::SliderFloat("Tri Scale", &currentMat->triplanarScale, 0.01f, 10.0f);
                }

                ImGui::TableSetColumnIndex(1);
                
               
                auto makeSetter = [&](auto func, std::shared_ptr<BurnhopeTexture> oldTex) {
                    return [=, this, &changed, &context](const std::string& p) mutable { 
                        func(p);
                        ClearThumbnailCache(); // Избавляемся от краша vkDestroySampler (ImGui держал старые кэши)
                    currentMat->packTexturesAsync();
                        changed = true;
                    };
                };

                if (ImGui::BeginTable("CardsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                    float* normSlider = currentMat->normalPath.empty() ? nullptr : &currentMat->normalStrength;
                    float* heightSlider = currentMat->heightPath.empty() ? nullptr : &currentMat->heightStrength;

                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Albedo (Base Color)", currentMat->albedoMap, currentMat->albedoPath, context, nullptr, 0, 0, &currentMat->albedoColor.x, true, false, true, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setAlbedo(nullptr, ""); else currentMat->setAlbedo(BurnhopeTexture::createTextureFromFile(*context.device, p), p); }, currentMat->albedoMap));
                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Alpha / Opacity", currentMat->alphaMap, currentMat->alphaPath, context, &currentMat->albedoColor.w, 0.0f, 1.0f, nullptr, false, false, true, &currentMat->isTransparent, "Transparent Material", changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setAlpha(nullptr, ""); else currentMat->setAlpha(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->alphaMap));

                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Metallic", currentMat->metallicMap, currentMat->metallicPath, context, &currentMat->metallicStrength, 0.0f, 1.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setMetallic(nullptr, ""); else { currentMat->setMetallic(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); currentMat->metallicStrength = 1.0f; } }, currentMat->metallicMap));
                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Roughness", currentMat->roughnessMap, currentMat->roughnessPath, context, &currentMat->roughnessStrength, 0.0f, 1.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setRoughness(nullptr, ""); else { currentMat->setRoughness(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); currentMat->roughnessStrength = 1.0f; } }, currentMat->roughnessMap));

                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Normal Map", currentMat->normalMap, currentMat->normalPath, context, normSlider, 0.0f, 5.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setNormal(nullptr, ""); else currentMat->setNormal(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->normalMap));
                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Height Map", currentMat->heightMap, currentMat->heightPath, context, heightSlider, 0.0f, 2.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setHeight(nullptr, ""); else currentMat->setHeight(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->heightMap));

                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Ambient Occlusion", currentMat->aoMap, currentMat->aoPath, context, &currentMat->aoStrength, 0.0f, 1.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setAO(nullptr, ""); else { currentMat->setAO(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); currentMat->aoStrength = 1.0f; } }, currentMat->aoMap));
                    ImGui::TableNextColumn();
                    DrawMaterialProperty("ORM Map (AO/Rough/Metal)", currentMat->ormMap, currentMat->ormPath, context, nullptr, 0.0f, 0.0f, nullptr, false, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setORM(nullptr, ""); else { currentMat->setORM(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); currentMat->aoStrength = 1.0f; currentMat->roughnessStrength = 1.0f; currentMat->metallicStrength = 1.0f; } }, currentMat->ormMap));

                    ImGui::TableNextColumn();
                    DrawMaterialProperty("Emission", currentMat->emissiveMap, currentMat->emissivePath, context, &currentMat->emissiveIntensity, 0.0f, 10.0f, &currentMat->emissiveColor.x, true, false, false, nullptr, nullptr, changed, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setEmissive(nullptr, ""); else currentMat->setEmissive(BurnhopeTexture::createTextureFromFile(*context.device, p), p); }, currentMat->emissiveMap));
                    
                    ImGui::EndTable();
                }

            ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.1f, 0.4f, 0.1f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (changed || ImGui::Button("💾 Save Material", ImVec2(-FLT_MIN, 30.0f))) {
                currentMat->saveToJson(currentPath);
                ReloadMaterialInScene(context, currentPath, currentMat);
                context.needsRebuild = true;
            }
            ImGui::PopStyleColor(2);

            ImGui::End();
        }
    };
}