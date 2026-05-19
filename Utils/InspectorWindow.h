#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <filesystem>
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"
#include "../Render/Texture.hpp"
#include "UIUtils.h"

namespace burnhope {
    class InspectorWindow : public IUIWindow {
    public:
        InspectorWindow() : IUIWindow("Scene Inspector") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

        if (context.selectedEntity.is_alive()) {
                DrawComponents(context, context.selectedEntity);
            } else if (context.selectedAssets.size() == 1 && context.selectedAssets[0].ends_with(".bhtex")) {
                DrawBHTexSettings(context, context.selectedAssets[0]);
            } else {
                ImGui::TextDisabled("No entity selected");
            }

            ImGui::End();
        }

    private:
        void DrawBHTexSettings(UIContext& context, const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return;
            BHTexHeader hdr; file.read(reinterpret_cast<char*>(&hdr), sizeof(BHTexHeader)); file.close();
            
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Texture Properties (.bhtex)");
            ImGui::Separator(); ImGui::Spacing();
            
            if (UIUtils::BeginPropertyGrid("Settings")) {
                UIUtils::DrawProperty("Is sRGB", &hdr.isSRGB);
                UIUtils::DrawProperty("Has Alpha", &hdr.hasAlpha);
                
                int filter = hdr.minFilter;
                if (UIUtils::DrawPropertyCombo("Filter Mode", &filter, "Nearest\0Linear\0")) hdr.minFilter = hdr.magFilter = filter;
                
                int wrap = hdr.wrapS;
                if (UIUtils::DrawPropertyCombo("Wrap Mode", &wrap, "Repeat\0Clamp To Edge\0")) hdr.wrapS = hdr.wrapT = wrap;
                UIUtils::EndPropertyGrid();
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Max Resolution: 2048x2048 (Lanczos)");
            ImGui::TextDisabled("Mipmaps: %d", hdr.mipCount);
            ImGui::TextDisabled("Format Pack: %d", hdr.packType);
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            if (ImGui::Button("Apply & Rebuild", ImVec2(-1, 30))) {
                // Сохраняем заголовок
                std::fstream out(path, std::ios::in | std::ios::out | std::ios::binary);
                out.write(reinterpret_cast<char*>(&hdr), sizeof(BHTexHeader));
                out.close();
                // Пересобираем
                BurnhopeTexture::rebuildFromHeader(path);
                context.needsRebuild = true;
            }
        }

        void DrawComponents(UIContext& context, flecs::entity entity) {
            if (entity.has<TagComponent>()) {
                auto& tag = entity.get_mut<TagComponent>();
                char buffer[256];
                strncpy(buffer, tag.name.c_str(), sizeof(buffer));
                if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                    tag.name = buffer;
                }
            }

            if (entity.has<TransformComponent>()) {
                if (UIUtils::BeginPropertyGrid("Transform")) {
                    auto& tc = entity.get_mut<TransformComponent>();
                    bool changed = false;
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Position"); ImGui::TableSetColumnIndex(1);
                    changed |= UIUtils::DrawVec3Control("##Pos", tc.transform.position, 0.0f);
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Rotation"); ImGui::TableSetColumnIndex(1);
                    changed |= UIUtils::DrawVec3Control("##Rot", tc.transform.rotation, 0.0f);
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Scale"); ImGui::TableSetColumnIndex(1);
                    changed |= UIUtils::DrawVec3Control("##Scl", tc.transform.scale, 1.0f);
                    if (changed) tc.transform.updatematrix = true;
                    UIUtils::EndPropertyGrid();
                }
            }

            if (entity.has<MeshComponent>()) {
                if (UIUtils::BeginPropertyGrid("Mesh Renderer")) {
                    auto& mc = entity.get_mut<MeshComponent>();
                    
                    UIUtils::DrawProperty("Is Visible", &mc.isVisible);
                    UIUtils::DrawProperty("Cast Shadow", &mc.castShadow);

                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Model:"); ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    std::string modelName = mc.modelPath.empty() ? "None" : std::filesystem::path(mc.modelPath).filename().string();
                    if (ImGui::Button((modelName + "##ModelBtn").c_str(), ImVec2(-1, 0))) {
                        ImGui::OpenPopup("SelectModelPopup");
                    }
                    
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* path = (const char*)payload->Data;
                            std::filesystem::path p(path);
                            if (p.extension() == ".bhmesh" || p.extension() == ".obj" || p.extension() == ".fbx" || p.extension() == ".gltf") {
                                context.pendingModelLoadPath = p.string();
                                context.pendingModelEntity = entity;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginPopup("SelectModelPopup")) {
                        auto models = context.GetProjectAssets({".bhmesh", ".obj", ".fbx", ".gltf"});
                        if (ImGui::Selectable("None")) {
                            context.pendingModelLoadPath = "NONE";
                            context.pendingModelEntity = entity;
                        }
                        for (const auto& m : models) {
                            if (ImGui::Selectable(std::filesystem::path(m).filename().string().c_str())) {
                                context.pendingModelLoadPath = m;
                                context.pendingModelEntity = entity;
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (mc.model) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Skeleton:"); ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        std::string skelName = mc.skeletonPath.empty() ? "None" : std::filesystem::path(mc.skeletonPath).filename().string();
                        ImGui::Button((skelName + "##SkelBtn").c_str(), ImVec2(-1, 0));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* path = (const char*)payload->Data;
                                std::filesystem::path p(path);
                                if (p.extension() == ".bhbone") {
                                    mc.skeletonPath = p.string();
                                    context.needsRebuild = true;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Animation:"); ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        std::string animName = mc.animationPath.empty() ? "None" : std::filesystem::path(mc.animationPath).filename().string();
                        ImGui::Button((animName + "##AnimBtn").c_str(), ImVec2(-1, 0));
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                const char* path = (const char*)payload->Data;
                                std::filesystem::path p(path);
                                if (p.extension() == ".bhanim") {
                                    mc.animationPath = p.string();
                                    context.needsRebuild = true;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        
                        if (!mc.animationPath.empty()) {
                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Anim Time:"); ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::DragFloat("##AnimTime", &mc.animationTime, 0.01f);
                        }

                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Materials:");
                        uint32_t matCount = mc.model->getMaterialCount();
                        for (uint32_t mId = 0; mId < matCount; mId++) {
                            size_t firstIdx = (size_t)-1;
                            for(size_t i = 0; i < mc.model->getSubMeshes().size(); i++) {
                                if (mc.model->getSubMeshes()[i].materialIndex == mId) { firstIdx = i; break; }
                            }
                            if (firstIdx == (size_t)-1) continue;

                            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("  Slot %u:", mId); ImGui::TableSetColumnIndex(1);
                            ImGui::SetNextItemWidth(-FLT_MIN);
                            ImGui::PushID(mId);
                            
                            std::string matName = "None";
                            if (firstIdx < mc.materialPaths.size() && !mc.materialPaths[firstIdx].empty()) {
                                matName = std::filesystem::path(mc.materialPaths[firstIdx]).filename().string();
                            }
                            
                            if (ImGui::Button((matName + "##MatBtn").c_str(), ImVec2(-1, 0))) {
                                ImGui::OpenPopup("SelectMatPopup");
                            }
                            
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    const char* path = (const char*)payload->Data;
                                    std::filesystem::path p(path);
                                    if (p.extension() == ".bhmat" || p.extension() == ".json") {
                                        context.pendingMatLoadPath = p.string();
                                        context.pendingMatSlot = mId;
                                        context.pendingMatEntity = entity;
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if (ImGui::BeginPopup("SelectMatPopup")) {
                                auto mats = context.GetProjectAssets({".bhmat", ".json"});
                                if (ImGui::Selectable("None")) {
                                    context.pendingMatLoadPath = "NONE";
                                    context.pendingMatSlot = mId;
                                    context.pendingMatEntity = entity;
                                }
                                for (const auto& m : mats) {
                                    if (ImGui::Selectable(std::filesystem::path(m).filename().string().c_str())) {
                                        context.pendingMatLoadPath = m;
                                        context.pendingMatSlot = mId;
                                        context.pendingMatEntity = entity;
                                    }
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }

                         ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                        ImGui::Text("Ray Tracing:"); ImGui::TableSetColumnIndex(1);
                        if (ImGui::Button("FORCE REBUILD ALL", ImVec2(-1, 0))) {
                            if (mc.model && mc.model->gpuDataReady && !mc.model->storedPositions.empty()) {
                                mc.model->createBLAS(mc.model->storedPositions);
                                context.needsRTRebuild = true;
                                context.needsRebuild = true; // Для обновления ObjectData/MaterialData
                            }
                        }
                    }
                    UIUtils::EndPropertyGrid();
                }
            }

            if (entity.has<LightComponent>()) {
                if (UIUtils::BeginPropertyGrid("Light")) {
                    auto& lc = entity.get_mut<LightComponent>();
                    UIUtils::DrawProperty("Enable", &lc.light.enable);
                    
                    const char* lightTypes[] = { "Directional", "Point", "Spot" };
                    int currentType = (int)lc.light.type;
                    if (UIUtils::DrawPropertyCombo("Type", &currentType, "Directional\0Point\0Spot\0")) {
                        lc.light.type = (LightType)currentType;
                    }

                    UIUtils::DrawPropertyColor("Color", &lc.light.color.x);
                    UIUtils::DrawProperty("Intensity", &lc.light.intensity, -1000.0f, 1000.0f);
                    
                    if (lc.light.type == LightType::Point || lc.light.type == LightType::Spot) {
                        UIUtils::DrawProperty("Radius", &lc.light.radius, 0.0f, 500.0f);
                    }
                    if (lc.light.type == LightType::Spot) {
                        UIUtils::DrawProperty("Inner Cone", &lc.light.innerCone, 0.0f, lc.light.outerCone);
                        UIUtils::DrawProperty("Outer Cone", &lc.light.outerCone, lc.light.innerCone, 90.0f);
                    }
                    
                    UIUtils::DrawProperty("Cast Shadows", &lc.light.castShadows);
                    UIUtils::EndPropertyGrid();
                }
            }

            if (entity.has<DecalComponent>()) {
                if (UIUtils::BeginPropertyGrid("Screen Space Decal")) {
                    auto& dc = entity.get_mut<DecalComponent>();
                    UIUtils::DrawProperty("Opacity", &dc.opacity, 0.0f, 1.0f);
                    
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Albedo Texture:"); ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    std::string albName = dc.albedoPath.empty() ? "None" : std::filesystem::path(dc.albedoPath).filename().string();
                    ImGui::Button((albName + "##DecalAlb").c_str(), ImVec2(-1, 0));
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* path = (const char*)payload->Data;
                            std::filesystem::path p(path);
                            if (p.extension() == ".png" || p.extension() == ".jpg" || p.extension() == ".jpeg") {
                                vkDeviceWaitIdle(context.device->device());
                                dc.albedoPath = p.string();
                                dc.albedoTex = BurnhopeTexture::createTextureFromFile(*context.device, dc.albedoPath);
                                context.needsRebuild = true;
                            }
                        } ImGui::EndDragDropTarget();
                    }
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Text("Normal Texture:"); ImGui::TableSetColumnIndex(1);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    std::string normName = dc.normalPath.empty() ? "None" : std::filesystem::path(dc.normalPath).filename().string();
                    ImGui::Button((normName + "##DecalNorm").c_str(), ImVec2(-1, 0));
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* path = (const char*)payload->Data;
                            std::filesystem::path p(path);
                            if (p.extension() == ".png" || p.extension() == ".jpg" || p.extension() == ".jpeg") {
                                vkDeviceWaitIdle(context.device->device());
                                dc.normalPath = p.string();
                                dc.normalTex = BurnhopeTexture::createDataTextureFromFile(*context.device, dc.normalPath);
                                context.needsRebuild = true;
                            }
                        } ImGui::EndDragDropTarget();
                    }
                    UIUtils::EndPropertyGrid();
                }
            }

            if (entity.has<ReflectionProbeComponent>()) {
                if (UIUtils::BeginPropertyGrid("Reflection Probe")) {
                    auto& pc = entity.get_mut<ReflectionProbeComponent>();
                    UIUtils::DrawProperty("Radius", &pc.radius, 0.1f, 500.0f);
                    int res = pc.resolution;
                    if (UIUtils::DrawProperty("Resolution", &res, 64, 2048)) {
                        pc.resolution = std::max(64, res);
                    }
                    UIUtils::EndPropertyGrid();
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponentPopup");
            }
            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (!entity.has<MeshComponent>() && ImGui::Selectable("Mesh Renderer")) {
                    entity.add<MeshComponent>();
                }
                if (!entity.has<LightComponent>() && ImGui::Selectable("Light")) {
                    entity.set<LightComponent>({});
                    entity.get_mut<LightComponent>().light.enable = true;
                }
                if (!entity.has<ReflectionProbeComponent>() && ImGui::Selectable("Reflection Probe")) {
                    entity.add<ReflectionProbeComponent>();
                }
                if (!entity.has<DecalComponent>() && ImGui::Selectable("Decal Projector")) {
                    entity.add<DecalComponent>();
                }
                ImGui::EndPopup();
            }
        }
    };
}