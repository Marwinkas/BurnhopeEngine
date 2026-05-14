#pragma once
#include "IUIWindow.h"
#include <imgui.h>

namespace burnhope {
    class OutlinerWindow : public IUIWindow {
    public:
        OutlinerWindow() : IUIWindow("Scene Outliner") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

            if (ImGui::Button("+ Add", ImVec2(60, 25))) ImGui::OpenPopup("GlobalCreateMenu");
            ImGui::SameLine();
            if (ImGui::Button("Unparent", ImVec2(80, 25)) && context.selectedEntity != entt::null) {
                context.SaveState();
                context.DetachFromParent(context.selectedEntity);
            }

            if (ImGui::BeginPopup("GlobalCreateMenu")) {
                if (ImGui::MenuItem("Empty Object")) { 
                    context.SaveState(); 
                    context.CreateBaseEntity("Empty"); 
                }
                if (ImGui::MenuItem("Mesh Object")) {
                    context.SaveState();
                    entt::entity e = context.CreateBaseEntity("Mesh");
                    context.registry->emplace<MeshComponent>(e);
                }
                if (ImGui::MenuItem("Light Source")) {
                    context.SaveState();
                    entt::entity e = context.CreateBaseEntity("Light");
                    context.registry->emplace<LightComponent>(e);
                }

                ImGui::EndPopup();
            }

            ImGui::Separator();
            ImGui::BeginChild("OutlinerList", ImVec2(0, -20));

            // Отрисовываем ТОЛЬКО корневые объекты
            auto view = context.registry->view<IDComponent, TagComponent>();
            for (auto entity : view) {
                bool isRoot = true;
                if (context.registry->all_of<HierarchyComponent>(entity)) {
                    if (context.registry->get<HierarchyComponent>(entity).parentID != 0) isRoot = false;
                }
                if (isRoot) DrawNode(context, entity);
            }

            // Dummy зона в самом низу окна для сброса родителя (Drag & Drop в пустоту)
            ImGui::Dummy(ImGui::GetContentRegionAvail());
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                    entt::entity dragged = *(const entt::entity*)payload->Data;
                    context.SaveState();
                    context.DetachFromParent(dragged); // Бросили в пустоту = отвязали
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::EndChild();
            ImGui::End();
        }

    private:
        void DrawNode(UIContext& context, entt::entity entity) {
            if (!context.registry->valid(entity)) return;
            auto& tag = context.registry->get<TagComponent>(entity);
            
            bool isLeaf = true;
            if (context.registry->all_of<HierarchyComponent>(entity)) {
                isLeaf = context.registry->get<HierarchyComponent>(entity).childrenIDs.empty();
            }

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (context.selectedEntity == entity) flags |= ImGuiTreeNodeFlags_Selected;
            if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf;

            bool nodeOpen = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, tag.name.c_str());

            if (ImGui::IsItemClicked()) {
                context.selectedEntity = entity;
            }

            // Меню по правому клику
            if (ImGui::BeginPopupContextItem()) {
                context.selectedEntity = entity;
                if (ImGui::BeginMenu("Create Child...")) {
                    if (ImGui::MenuItem("Empty Object")) {
                        context.SaveState();
                        entt::entity newE = context.CreateBaseEntity("Empty");
                        context.AttachToParent(newE, entity);
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del")) { 
                    context.SaveState(); 
                    context.DeleteEntityRecursive(entity); 
                }
                ImGui::EndPopup();
            }

            // Drag & Drop Source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("OUTLINER_NODE", &entity, sizeof(entt::entity));
                ImGui::Text("Move %s", tag.name.c_str());
                ImGui::EndDragDropSource();
            }
            
            // Drag & Drop Target
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                    entt::entity dragged = *(const entt::entity*)payload->Data;
                    context.SaveState();
                    context.AttachToParent(dragged, entity);
                }
                ImGui::EndDragDropTarget();
            }

            // Рекурсивная отрисовка детей
            if (nodeOpen) {
                if (!isLeaf) {
                    auto children = context.registry->get<HierarchyComponent>(entity).childrenIDs;
                    for (uint64_t childID : children) {
                        entt::entity childEnt = context.FindEntityByID(childID);
                        if (childEnt != entt::null) DrawNode(context, childEnt);
                    }
                }
                ImGui::TreePop();
            }
        }
    };
}