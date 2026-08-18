#pragma once
#include "IUIWindow.h"
#include <filesystem>
#include <fstream>
#include <any>
#include <algorithm>
#include "../Render/Model.hpp"
#include "../Render/Material.hpp"
#include "../Render/Texture.hpp"

namespace burnhope {
    class InspectorWindow : public IUIWindow {
    public:
        InspectorWindow() : IUIWindow("Scene Inspector") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            ui::Panel panel(widgets, m_Name, contentRect);

            if (context.selectedEntity.is_alive()) {
                DrawComponents(context, widgets, context.selectedEntity);
            } else if (context.selectedAssets.size() == 1 &&
                       std::filesystem::path(context.selectedAssets.front()).extension() == ".bhtex") {
                DrawTextureSettings(context, widgets, context.selectedAssets.front());
            } else {
                widgets.Text("No entity selected", ui::kTheme.textMuted);
            }
        }

    private:
        void DrawTextureSettings(UIContext& context, ui::UIWidgets& widgets, const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            BHTexHeader header{};
            if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return;
            widgets.Text("Texture Properties (.bhtex)", {0.3f, 0.8f, 0.3f, 1.0f});
            widgets.Separator();
            widgets.DrawCheckboxControl("sRGB", &header.isSRGB);
            widgets.DrawCheckboxControl("Has Alpha", &header.hasAlpha);
            widgets.Text("Mipmaps: " + std::to_string(header.mipCount));
            widgets.Text("Format Pack: " + std::to_string(header.packType),
                         {0.55f, 0.55f, 0.55f, 1.0f});
            if (widgets.Button("Apply & Rebuild", {0, 28})) {
                std::fstream out(path, std::ios::in | std::ios::out | std::ios::binary);
                out.write(reinterpret_cast<const char*>(&header), sizeof(header));
                out.close();
                BurnhopeTexture::rebuildFromHeader(path);
                context.needsRebuild = true;
            }
        }

        // Selects an asset path via a simple popup listing every project
        // asset matching `extensions` (used instead of ImGui drag/drop
        // combined with a fallback popup — kept as a popup-only picker here
        // to stay within the immediate-mode widget set built for this pass).
        bool AssetPicker(ui::UIWidgets& widgets, UIContext& context, const std::string& label,
                          const std::string& currentPath, const std::vector<std::string>& extensions,
                          std::string& outPath) {
            widgets.PushID(label);
            widgets.Text(label);
            std::string btnLabel = currentPath.empty() ? "None" : std::filesystem::path(currentPath).filename().string();
            bool clicked = widgets.Button(btnLabel, {200, 24});
            if (clicked) widgets.OpenPopup("AssetPickerPopup");
            bool picked = false;
            if (widgets.BeginDragDropTarget()) {
                if (const auto* payload = widgets.AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    const std::string* path = std::any_cast<std::string>(payload);
                    if (path) {
                        const std::string ext = std::filesystem::path(*path).extension().string();
                        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end()) {
                            outPath = *path;
                            picked = true;
                        }
                    }
                }
                widgets.EndDragDropTarget();
            }
            if (widgets.BeginPopup("AssetPickerPopup")) {
                if (widgets.Selectable("None", false)) { outPath = "NONE"; picked = true; widgets.CloseCurrentPopup(); }
                for (const auto& a : context.GetProjectAssets(extensions)) {
                    if (widgets.Selectable(std::filesystem::path(a).filename().string(), false)) {
                        outPath = a;
                        picked = true;
                        widgets.CloseCurrentPopup();
                    }
                }
                widgets.EndPopup();
            }
            widgets.PopID();
            return picked;
        }

        void DrawComponents(UIContext& context, ui::UIWidgets& widgets, flecs::entity entity) {
            if (entity.has<TagComponent>()) {
                auto& tag = *entity.get_mut<TagComponent>();
                widgets.InputText("Name", tag.name, 256);
                widgets.Separator();
            }

            if (transform::hasBundle(entity)) {
                widgets.Text("Transform");
                auto* position = entity.get_mut<Position3>();
                auto* rotation = entity.get_mut<RotationEuler>();
                auto* scale = entity.get_mut<Scale3>();
                auto* matrix = entity.get_mut<LocalMatrix>();
                bool changed = false;
                changed |= widgets.DrawVec3Control("Position", transform::asVec3Mut(*position), 0.0f);
                changed |= widgets.DrawVec3Control("Rotation", transform::asVec3Mut(*rotation), 0.0f);
                changed |= widgets.DrawVec3Control("Scale", transform::asVec3Mut(*scale), 1.0f);
                if (changed) transform::markDirty(*matrix);
                widgets.Separator();
            }

            if (entity.has<MeshComponent>()) {
                widgets.Text("Mesh Renderer");
                auto& mc = *entity.get_mut<MeshComponent>();
                widgets.DrawCheckboxControl("Is Visible", &mc.isVisible);
                widgets.DrawCheckboxControl("Cast Shadow", &mc.castShadow);

                std::string modelPick;
                if (AssetPicker(widgets, context, "Model", mc.modelPath, {".bhmesh", ".obj", ".fbx", ".gltf"}, modelPick)) {
                    context.pendingModelLoadPath = modelPick;
                    context.pendingModelEntity = entity;
                }

                if (mc.model) {
                    std::string skeletonPick;
                    if (AssetPicker(widgets, context, "Skeleton", mc.skeletonPath, {".bhbone"}, skeletonPick)) {
                        mc.skeletonPath = skeletonPick == "NONE" ? "" : skeletonPick;
                        context.needsRebuild = true;
                    }
                    std::string animationPick;
                    if (AssetPicker(widgets, context, "Animation", mc.animationPath, {".bhanim"}, animationPick)) {
                        mc.animationPath = animationPick == "NONE" ? "" : animationPick;
                        context.needsRebuild = true;
                    }
                    if (!mc.animationPath.empty()) {
                        widgets.DrawFloatControl("Animation Time", &mc.animationTime, 0.0f, 0.01f);
                    }
                    uint32_t matCount = mc.model->getMaterialCount();
                    for (uint32_t mId = 0; mId < matCount; mId++) {
                        size_t firstIdx = static_cast<size_t>(-1);
                        for (size_t i = 0; i < mc.model->getSubMeshes().size(); i++) {
                            if (mc.model->getSubMeshes()[i].materialIndex == mId) { firstIdx = i; break; }
                        }
                        if (firstIdx == static_cast<size_t>(-1)) continue;

                        widgets.PushIDInt(mId);
                        std::string curMat = (firstIdx < mc.materialPaths.size()) ? mc.materialPaths[firstIdx] : "";
                        std::string matPick;
                        if (AssetPicker(widgets, context, "Material Slot " + std::to_string(mId), curMat, {".bhmat", ".json"}, matPick)) {
                            context.pendingMatLoadPath = matPick;
                            context.pendingMatSlot = mId;
                            context.pendingMatEntity = entity;
                        }
                        widgets.PopID();
                    }

                    if (widgets.Button("Force Rebuild RT (BLAS)", {220, 24})) {
                        if (mc.model->gpuDataReady && !mc.model->storedPositions.empty()) {
                            mc.model->createBLAS(mc.model->storedPositions);
                            context.needsRTRebuild = true;
                            context.needsRebuild = true;
                        }
                    }
                }
                widgets.Separator();
            }

            if (entity.has<LightComponent>()) {
                widgets.Text("Light");
                auto& lc = *entity.get_mut<LightComponent>();
                widgets.DrawCheckboxControl("Enable", &lc.light.enable);

                const char* lightTypeNames[] = {"Directional", "Point", "Spot"};
                std::string preview = lightTypeNames[std::clamp(static_cast<int>(lc.light.type), 0, 2)];
                if (ui::Combo type(widgets, "Type", preview); type) {
                    for (int i = 0; i < 3; ++i) {
                        if (widgets.ComboItem(lightTypeNames[i], static_cast<int>(lc.light.type) == i)) {
                            lc.light.type = static_cast<LightType>(i);
                        }
                    }
                }

                widgets.DrawColorControl("Color", lc.light.color);
                widgets.DrawFloatControl("Intensity", &lc.light.intensity);
                if (lc.light.type == LightType::Point || lc.light.type == LightType::Spot) {
                    widgets.DrawFloatControl("Radius", &lc.light.radius);
                }
                if (lc.light.type == LightType::Spot) {
                    widgets.DrawFloatControl("Inner Cone", &lc.light.innerCone, 0.0f, 0.1f);
                    widgets.DrawFloatControl("Outer Cone", &lc.light.outerCone, lc.light.innerCone, 0.1f);
                    lc.light.innerCone = std::clamp(lc.light.innerCone, 0.0f, lc.light.outerCone);
                    lc.light.outerCone = std::clamp(lc.light.outerCone, lc.light.innerCone, 90.0f);
                }
                widgets.DrawCheckboxControl("Cast Shadows", &lc.light.castShadows);
                widgets.Separator();
            }

            if (entity.has<DecalComponent>()) {
                widgets.Text("Screen Space Decal");
                auto& dc = *entity.get_mut<DecalComponent>();
                widgets.DrawFloatControl("Opacity", &dc.opacity);

                std::string albedoPick;
                if (AssetPicker(widgets, context, "Albedo Texture", dc.albedoPath, {".png", ".jpg", ".jpeg"}, albedoPick)) {
                    vkDeviceWaitIdle(context.device->device());
                    dc.albedoPath = albedoPick == "NONE" ? "" : albedoPick;
                    dc.albedoTex = dc.albedoPath.empty() ? nullptr : BurnhopeTexture::createTextureFromFile(*context.device, dc.albedoPath);
                    context.needsRebuild = true;
                }
                std::string normalPick;
                if (AssetPicker(widgets, context, "Normal Texture", dc.normalPath, {".png", ".jpg", ".jpeg"}, normalPick)) {
                    vkDeviceWaitIdle(context.device->device());
                    dc.normalPath = normalPick == "NONE" ? "" : normalPick;
                    dc.normalTex = dc.normalPath.empty() ? nullptr : BurnhopeTexture::createDataTextureFromFile(*context.device, dc.normalPath);
                    context.needsRebuild = true;
                }
                widgets.Separator();
            }

            if (entity.has<ReflectionProbeComponent>()) {
                widgets.Text("Reflection Probe");
                auto& pc = *entity.get_mut<ReflectionProbeComponent>();
                widgets.DrawFloatControl("Radius", &pc.radius);
                widgets.DrawIntControl("Resolution", &pc.resolution);
                widgets.Separator();
            }

            if (widgets.Button("Add Component", {150, 26})) widgets.OpenPopup("AddComponentPopup");
            if (widgets.BeginPopup("AddComponentPopup")) {
                if (!entity.has<MeshComponent>() && widgets.MenuItem("Mesh Renderer")) { entity.set<MeshComponent>({}); widgets.CloseCurrentPopup(); }
                if (!entity.has<LightComponent>() && widgets.MenuItem("Light")) {
                    LightComponent lc; lc.light.enable = true; entity.set<LightComponent>(lc);
                    widgets.CloseCurrentPopup();
                }
                if (!entity.has<ReflectionProbeComponent>() && widgets.MenuItem("Reflection Probe")) { entity.set<ReflectionProbeComponent>({}); widgets.CloseCurrentPopup(); }
                if (!entity.has<DecalComponent>() && widgets.MenuItem("Decal Projector")) { entity.set<DecalComponent>({}); widgets.CloseCurrentPopup(); }
                widgets.EndPopup();
            }
        }
    };
}
