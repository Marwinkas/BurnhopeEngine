#pragma once
#include "IUIWindow.h"

namespace burnhope {
    class OutlinerWindow : public IUIWindow {
    public:
        OutlinerWindow() : IUIWindow("Scene Outliner") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            ui::Panel panel(widgets, m_Name, contentRect);

            if (widgets.Button("+ Add", {72, 24})) widgets.OpenPopup("GlobalCreateMenu");
            widgets.SameLine();
            if (widgets.Button("Unparent", {86, 24}) && context.selectedEntity.is_alive()) {
                context.SaveState();
                context.DetachFromParent(context.selectedEntity);
            }

            if (ui::Popup add(widgets, "GlobalCreateMenu"); add) {
                if (widgets.MenuItem("Empty Object")) {
                    context.SaveState();
                    context.SelectOnly(context.CreateBaseEntity("Empty"));
                    widgets.CloseCurrentPopup();
                }
                if (widgets.MenuItem("Mesh Object")) {
                    context.SaveState();
                    flecs::entity e = context.CreateBaseEntity("Mesh");
                    e.set<MeshComponent>({});
                    context.SelectOnly(e);
                    widgets.CloseCurrentPopup();
                }
                if (widgets.MenuItem("Light Source")) {
                    context.SaveState();
                    flecs::entity e = context.CreateBaseEntity("Light");
                    e.set<LightComponent>({});
                    context.SelectOnly(e);
                    widgets.CloseCurrentPopup();
                }
            }

            widgets.Separator();

            context.world->each<IDComponent>([&](flecs::entity entity, IDComponent&) {
                if (!entity.has<TagComponent>()) return;
                bool isRoot = true;
                if (entity.has<HierarchyComponent>()) {
                    if (entity.get<HierarchyComponent>()->parentID != 0) isRoot = false;
                }
                if (isRoot) DrawNode(context, widgets, entity);
            });

            // A real drop target at the bottom mirrors ImGui's empty-child
            // target: dropping a node here removes its parent.
            if (widgets.Selectable("Drop here to unparent", false, {0, 22}) &&
                context.selectedEntity.is_alive()) {
                context.SaveState();
                context.DetachFromParent(context.selectedEntity);
            }
        }

    private:
        void DrawNode(UIContext& context, ui::UIWidgets& widgets, flecs::entity entity) {
            if (!entity.is_alive()) return;
            auto& tag = *entity.get_mut<TagComponent>();

            bool isLeaf = true;
            if (entity.has<HierarchyComponent>()) {
                isLeaf = entity.get<HierarchyComponent>()->childrenIDs.empty();
            }

            ui::ID nodeId(widgets, entity.id());
            bool selected = context.IsSelected(entity);
            ui::Tree tree(widgets, tag.name, selected, isLeaf);
            if (widgets.WasItemClicked()) {
                if (widgets.Ctrl()) context.ToggleSelect(entity);
                else context.SelectOnly(entity);
            }
            if (widgets.WasItemRightClicked()) {
                context.SelectOnly(entity);
                widgets.OpenPopup("NodeContext");
            }

            if (widgets.BeginDragDropSource()) {
                widgets.SetDragDropPayload("OUTLINER_NODE", entity);
                widgets.EndDragDropSource();
            }
            if (widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("OUTLINER_NODE")) {
                    flecs::entity dragged = std::any_cast<flecs::entity>(*payload);
                    context.SaveState();
                    context.AttachToParent(dragged, entity);
                }
                widgets.EndDragDropTarget();
            }

            if (ui::Popup ctx(widgets, "NodeContext"); ctx) {
                if (widgets.MenuItem("Create Child")) {
                    context.SaveState();
                    flecs::entity child = context.CreateBaseEntity("Empty");
                    context.AttachToParent(child, entity);
                    widgets.CloseCurrentPopup();
                }
                if (widgets.MenuItem("Delete")) {
                    context.SaveState();
                    context.DeleteEntityRecursive(entity);
                    widgets.CloseCurrentPopup();
                }
            }

            if (tree) {
                auto children = entity.get<HierarchyComponent>()->childrenIDs;
                for (uint64_t childID : children) {
                    flecs::entity childEnt = context.FindEntityByID(childID);
                    if (childEnt.is_alive()) DrawNode(context, widgets, childEnt);
                }
            }
        }
    };
}
