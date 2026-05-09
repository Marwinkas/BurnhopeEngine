#pragma once
#include "IUIWindow.h"
#include "UIUtils.h"
#include <imgui.h>
#include <cstring>
#include <iostream>
#include "ImGuizmo.h" // Для RecomposeMatrixFromComponents

namespace burnhope {
    class InspectorWindow : public IUIWindow {
    public:
        InspectorWindow() : IUIWindow("Scene Inspector") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

            if (context.selectedEntity == entt::null || !context.registry->valid(context.selectedEntity)) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an object in the scene");
                ImGui::End();
                return;
            }

            auto& tag = context.registry->get<TagComponent>(context.selectedEntity);
            char nameBuf[128];
            strncpy(nameBuf, tag.name.c_str(), sizeof(nameBuf));
            nameBuf[sizeof(nameBuf) - 1] = '\0'; // Безопасное завершение строки

            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) {
                tag.name = nameBuf;
            }
            ImGui::PopItemWidth();
            ImGui::Spacing();

            // Transform Component
            if (context.registry->all_of<TransformComponent>(context.selectedEntity)) {
                auto& tComp = context.registry->get<TransformComponent>(context.selectedEntity);
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                    bool changed = false;
                    
                    changed |= UIUtils::DrawVec3Control("Position", tComp.transform.position);
                    if (ImGui::IsItemActivated()) context.SaveState();
                    
                    changed |= UIUtils::DrawVec3Control("Rotation", tComp.transform.rotation);
                    if (ImGui::IsItemActivated()) context.SaveState();
                    
                    changed |= UIUtils::DrawVec3Control("Scale", tComp.transform.scale, 1.0f);
                    if (ImGui::IsItemActivated()) context.SaveState();

                    if (changed) {
                        tComp.transform.updatematrix = true;
                        ImGuizmo::RecomposeMatrixFromComponents(
                            glm::value_ptr(tComp.transform.position), 
                            glm::value_ptr(tComp.transform.rotation), 
                            glm::value_ptr(tComp.transform.scale), 
                            glm::value_ptr(context.modelMatrix)
                        );
                    }
                }
            }

            // Light Component
            if (context.registry->all_of<LightComponent>(context.selectedEntity)) {
                auto& lComp = context.registry->get<LightComponent>(context.selectedEntity).light;
                bool removeLight = false;
                
                if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap)) {
                    ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                    if (ImGui::Button("X##RM_LIGHT")) removeLight = true;
                    
                    ImGui::Checkbox("Enable Light", &lComp.enable);
                    
                    const char* lightTypes[] = {"Directional", "Point", "Spot", "Rect", "Sky"};
                    int currentType = (int)lComp.type;
                    if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes))) {
                        context.SaveState();
                        lComp.type = (LightType)currentType;
                    }
                    
                    const char* lightMobilities[] = {"Static", "Movable"};
                    int currentMobility = (int)lComp.mobility;
                    if (ImGui::Combo("Mobility", &currentMobility, lightMobilities, IM_ARRAYSIZE(lightMobilities))) {
                        context.SaveState();
                        lComp.mobility = (LightMobility)currentMobility;
                    }
                    
                    ImGui::ColorEdit3("Color", glm::value_ptr(lComp.color));
                    ImGui::DragFloat("Intensity", &lComp.intensity, 0.1f, 0.0f, 1000.0f);
                    
                    if (lComp.type == LightType::Point || lComp.type == LightType::Spot) {
                        ImGui::DragFloat("Radius", &lComp.radius, 0.5f, 0.1f, 500.0f);
                    }
                    if (lComp.type == LightType::Spot) {
                        ImGui::DragFloat("Inner Angle", &lComp.innerCone, 0.5f, 0.0f, lComp.outerCone);
                        ImGui::DragFloat("Outer Angle", &lComp.outerCone, 0.5f, lComp.innerCone, 90.0f);
                    }
                    
                    ImGui::Checkbox("Cast Shadows", &lComp.castShadows);
                }
                
                if (removeLight) {
                    context.SaveState();
                    context.registry->erase<LightComponent>(context.selectedEntity);
                }
            }

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("Add Component", ImVec2(-1, 30))) ImGui::OpenPopup("AddComponentPopup");
            if (ImGui::BeginPopup("AddComponentPopup")) {
                if (!context.registry->all_of<MeshComponent>(context.selectedEntity) && ImGui::MenuItem("Mesh Renderer")) {
                    context.SaveState(); 
                    context.registry->emplace<MeshComponent>(context.selectedEntity);
                }
                if (!context.registry->all_of<LightComponent>(context.selectedEntity) && ImGui::MenuItem("Light Component")) {
                    context.SaveState(); 
                    context.registry->emplace<LightComponent>(context.selectedEntity);
                }
                ImGui::EndPopup();
            }
            
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            
            if (ImGui::Button("💾 SAVE SCENE", ImVec2(-1, 40))) {
                std::cout << "Need to update Serializer for EnTT!\n";
            }

            ImGui::End();
        }
    };
}