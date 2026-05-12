#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <filesystem>
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"

namespace burnhope {
    class InspectorWindow : public IUIWindow {
    public:
        InspectorWindow() : IUIWindow("Scene Inspector") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

            if (context.selectedEntity != entt::null && context.registry->valid(context.selectedEntity)) {
                DrawComponents(context, context.selectedEntity);
            } else {
                ImGui::TextDisabled("No entity selected");
            }

            ImGui::End();
        }

    private:
        void DrawComponents(UIContext& context, entt::entity entity) {
            if (context.registry->all_of<TagComponent>(entity)) {
                auto& tag = context.registry->get<TagComponent>(entity);
                char buffer[256];
                strncpy(buffer, tag.name.c_str(), sizeof(buffer));
                if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                    tag.name = buffer;
                }
            }

            if (context.registry->all_of<TransformComponent>(entity)) {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& tc = context.registry->get<TransformComponent>(entity);
                    bool changed = false;
                    changed |= ImGui::DragFloat3("Position", &tc.transform.position.x, 0.1f);
                    changed |= ImGui::DragFloat3("Rotation", &tc.transform.rotation.x, 0.5f);
                    changed |= ImGui::DragFloat3("Scale", &tc.transform.scale.x, 0.1f);
                    if (changed) tc.transform.updatematrix = true;
                }
            }

            if (context.registry->all_of<MeshComponent>(entity)) {
                if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& mc = context.registry->get<MeshComponent>(entity);
                    
                    ImGui::Checkbox("Is Visible", &mc.isVisible);
                    ImGui::Checkbox("Cast Shadow", &mc.castShadow);

                    ImGui::Text("Model:"); ImGui::SameLine();
                    std::string modelName = mc.modelPath.empty() ? "None" : std::filesystem::path(mc.modelPath).filename().string();
                    if (ImGui::Button((modelName + "##ModelBtn").c_str(), ImVec2(-1, 0))) {
                        ImGui::OpenPopup("SelectModelPopup");
                    }
                    
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                            const char* path = (const char*)payload->Data;
                            std::filesystem::path p(path);
                            if (p.extension() == ".bhmesh" || p.extension() == ".obj" || p.extension() == ".fbx" || p.extension() == ".gltf") {
                                vkDeviceWaitIdle(context.device->device());
                                context.safeDeleteQueue.push_back(mc.model);
                                mc.modelPath = p.string();
                                mc.model = BurnhopeModel::createModelFromFile(*context.device, mc.modelPath);
                                mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                                mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                                context.needsRebuild = true;
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (ImGui::BeginPopup("SelectModelPopup")) {
                        auto models = context.GetProjectAssets({".bhmesh", ".obj", ".fbx", ".gltf"});
                        if (ImGui::Selectable("None")) {
                            vkDeviceWaitIdle(context.device->device());
                            context.safeDeleteQueue.push_back(mc.model);
                            mc.modelPath = "";
                            mc.model = nullptr;
                            mc.materialPaths.clear();
                            mc.materials.clear();
                            context.needsRebuild = true;
                        }
                        for (const auto& m : models) {
                            if (ImGui::Selectable(std::filesystem::path(m).filename().string().c_str())) {
                                vkDeviceWaitIdle(context.device->device());
                                context.safeDeleteQueue.push_back(mc.model);
                                mc.modelPath = m;
                                mc.model = BurnhopeModel::createModelFromFile(*context.device, m);
                                mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                                mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                                context.needsRebuild = true;
                            }
                        }
                        ImGui::EndPopup();
                    }

                    if (mc.model) {
                        ImGui::Text("Materials:");
                        for (size_t i = 0; i < mc.materialPaths.size(); i++) {
                            ImGui::PushID(i);
                            std::string matName = mc.materialPaths[i].empty() ? "None" : std::filesystem::path(mc.materialPaths[i]).filename().string();
                            if (ImGui::Button((matName + "##MatBtn").c_str(), ImVec2(-1, 0))) {
                                ImGui::OpenPopup("SelectMatPopup");
                            }
                            
                            if (ImGui::BeginDragDropTarget()) {
                                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                                    const char* path = (const char*)payload->Data;
                                    std::filesystem::path p(path);
                                    if (p.extension() == ".bhmat" || p.extension() == ".json") {
                                        vkDeviceWaitIdle(context.device->device());
                                        context.safeDeleteQueue.push_back(mc.materials[i]);
                                        mc.materialPaths[i] = p.string();
                                        mc.materials[i] = Material::loadFromJson(*context.device, p.string());
                                        
                                        context.needsRebuild = true;
                                    }
                                }
                                ImGui::EndDragDropTarget();
                            }

                            if (ImGui::BeginPopup("SelectMatPopup")) {
                                auto mats = context.GetProjectAssets({".bhmat", ".json"});
                                if (ImGui::Selectable("None")) {
                                    vkDeviceWaitIdle(context.device->device());
                                    context.safeDeleteQueue.push_back(mc.materials[i]);
                                    mc.materialPaths[i] = "";
                                    mc.materials[i] = nullptr;
                                    context.needsRebuild = true;
                                }
                                for (const auto& m : mats) {
                                    if (ImGui::Selectable(std::filesystem::path(m).filename().string().c_str())) {
                                        vkDeviceWaitIdle(context.device->device());
                                        context.safeDeleteQueue.push_back(mc.materials[i]);
                                        mc.materialPaths[i] = m;
                                        mc.materials[i] = Material::loadFromJson(*context.device, m);
                                        context.needsRebuild = true;
                                    }
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                    }
                }
            }

            if (context.registry->all_of<LightComponent>(entity)) {
                if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& lc = context.registry->get<LightComponent>(entity);
                    ImGui::Checkbox("Enable", &lc.light.enable);
                    
                    const char* lightTypes[] = { "Directional", "Point", "Spot" };
                    int currentType = (int)lc.light.type;
                    if (ImGui::Combo("Type", &currentType, lightTypes, IM_COUNTOF(lightTypes))) {
                        lc.light.type = (LightType)currentType;
                    }

                    ImGui::ColorEdit3("Color", &lc.light.color.x);
                    ImGui::DragFloat("Intensity", &lc.light.intensity, 0.1f, 0.0f, 1000.0f);
                    
                    if (lc.light.type == LightType::Point || lc.light.type == LightType::Spot) {
                        ImGui::DragFloat("Radius", &lc.light.radius, 0.5f, 0.0f);
                    }
                    if (lc.light.type == LightType::Spot) {
                        ImGui::DragFloat("Inner Cone", &lc.light.innerCone, 0.1f, 0.0f, lc.light.outerCone);
                        ImGui::DragFloat("Outer Cone", &lc.light.outerCone, 0.1f, lc.light.innerCone, 90.0f);
                    }
                    
                    ImGui::Checkbox("Cast Shadows", &lc.light.castShadows);
                }
            }

            if (context.registry->all_of<DecalComponent>(entity)) {
                if (ImGui::CollapsingHeader("Screen Space Decal", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& dc = context.registry->get<DecalComponent>(entity);
                    ImGui::SliderFloat("Opacity", &dc.opacity, 0.0f, 1.0f);
                    
                    ImGui::Text("Albedo Texture:"); ImGui::SameLine();
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
                    ImGui::Text("Normal Texture:"); ImGui::SameLine();
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
                }
            }

            if (context.registry->all_of<ReflectionProbeComponent>(entity)) {
                if (ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
                    auto& pc = context.registry->get<ReflectionProbeComponent>(entity);
                    ImGui::DragFloat("Radius", &pc.radius, 0.5f, 0.1f);
                    int res = pc.resolution;
                    if (ImGui::InputInt("Resolution", &res)) {
                        pc.resolution = std::max(64, res);
                    }
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Add Component")) {
                ImGui::OpenPopup("AddComponentPopup");
            }
            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (!context.registry->all_of<MeshComponent>(entity) && ImGui::Selectable("Mesh Renderer")) {
                    context.registry->emplace<MeshComponent>(entity);
                }
                if (!context.registry->all_of<LightComponent>(entity) && ImGui::Selectable("Light")) {
                    context.registry->emplace<LightComponent>(entity).light.enable = true;
                }
                if (!context.registry->all_of<ReflectionProbeComponent>(entity) && ImGui::Selectable("Reflection Probe")) {
                    context.registry->emplace<ReflectionProbeComponent>(entity);
                }
                if (!context.registry->all_of<DecalComponent>(entity) && ImGui::Selectable("Decal Projector")) {
                    context.registry->emplace<DecalComponent>(entity);
                }
                ImGui::EndPopup();
            }
        }
    };
}