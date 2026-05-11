#pragma once
#include "IUIWindow.h"
#include <imgui.h>
#include <filesystem>
#include "../Render/Material.hpp"
#include "../Render/Texture.hpp"

namespace burnhope {
    class MaterialEditorWindow : public IUIWindow {
    private:
        std::shared_ptr<Material> currentMat;
        std::string currentPath;

    public:
        MaterialEditorWindow() : IUIWindow("Material Editor") {}

        void Draw(UIContext& context) override {
            if (!m_IsOpen) return;
            ImGui::Begin(m_Name.c_str(), &m_IsOpen);

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
                currentPath = selectedPath;
                if (currentMat) context.safeDeleteQueue.push_back(currentMat);
                currentMat = Material::loadFromJson(*context.device, currentPath);
                if (!currentMat) {
                    currentMat = std::make_shared<Material>();
                }
            }

            if (!currentMat) {
                ImGui::End();
                return;
            }

            ImGui::Text("Editing: %s", std::filesystem::path(currentPath).filename().string().c_str());
            ImGui::Separator();

            bool changed = false;

            changed |= ImGui::ColorEdit3("Albedo Color", &currentMat->albedoColor.x);
            changed |= ImGui::ColorEdit3("Emissive Color", &currentMat->emissiveColor.x);
            changed |= ImGui::SliderFloat("Metallic Strength", &currentMat->metallicStrength, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Roughness Strength", &currentMat->roughnessStrength, 0.0f, 1.0f);
            changed |= ImGui::SliderFloat("Normal Strength", &currentMat->normalStrength, 0.0f, 5.0f);
            changed |= ImGui::SliderFloat("Height Strength", &currentMat->heightStrength, 0.0f, 2.0f);
            changed |= ImGui::SliderFloat("AO Strength", &currentMat->aoStrength, 0.0f, 1.0f);

            changed |= ImGui::DragFloat2("UV Scale", &currentMat->uvScale.x, 0.01f);
            changed |= ImGui::DragFloat("Emissive Intensity", &currentMat->emissiveIntensity, 0.1f);
            changed |= ImGui::Checkbox("Is ORM Texture", &currentMat->isORM);
            changed |= ImGui::Checkbox("Repeat Texture (Tiling)", &currentMat->repeatTexture);
            changed |= ImGui::Checkbox("World Aligned Texture (Triplanar)", &currentMat->useTriplanar);
            if (currentMat->useTriplanar) changed |= ImGui::DragFloat("World Aligned Scale", &currentMat->triplanarScale, 0.01f);

            auto makeSetter = [&](auto func, std::shared_ptr<BurnhopeTexture> oldTex) {
                return [=, &changed, &context](const std::string& p) mutable { 
                    vkDeviceWaitIdle(context.device->device());
                    if (oldTex) context.safeDeleteQueue.push_back(oldTex);
                    func(p);
                    changed = true;
                    context.needsRebuild = true;
                };
            };

            DrawTextureSlot("Albedo Map", currentMat->albedoPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setAlbedo(nullptr, ""); else currentMat->setAlbedo(BurnhopeTexture::createTextureFromFile(*context.device, p), p); }, currentMat->albedoMap));
            DrawTextureSlot("Normal Map", currentMat->normalPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setNormal(nullptr, ""); else currentMat->setNormal(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->normalMap));
            DrawTextureSlot("Metallic Map", currentMat->metallicPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setMetallic(nullptr, ""); else currentMat->setMetallic(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->metallicMap));
            DrawTextureSlot("Roughness Map", currentMat->roughnessPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setRoughness(nullptr, ""); else currentMat->setRoughness(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->roughnessMap));
            DrawTextureSlot("AO Map", currentMat->aoPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setAO(nullptr, ""); else currentMat->setAO(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->aoMap));
            DrawTextureSlot("Emissive Map", currentMat->emissivePath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setEmissive(nullptr, ""); else currentMat->setEmissive(BurnhopeTexture::createTextureFromFile(*context.device, p), p); }, currentMat->emissiveMap));
            DrawTextureSlot("Height Map", currentMat->heightPath, context, makeSetter([&](const std::string& p){ if(p.empty()) currentMat->setHeight(nullptr, ""); else currentMat->setHeight(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); }, currentMat->heightMap));

            if (changed || ImGui::Button("Save Material")) {
                currentMat->saveToJson(currentPath);
                ReloadMaterialInScene(context, currentPath, currentMat);
                context.needsRebuild = true;
            }

            ImGui::End();
        }

    private:
        template<typename F>
        void DrawTextureSlot(const char* label, std::string& currentTexPath, UIContext& context, F setter) {
            ImGui::PushID(label);
            ImGui::Text("%s:", label);
            ImGui::SameLine();
            std::string texName = currentTexPath.empty() ? "None" : std::filesystem::path(currentTexPath).filename().string();
            if (ImGui::Button((texName + "##TexBtn").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 30, 0))) {
                ImGui::OpenPopup("SelectTexPopup");
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
            if (ImGui::Button("X", ImVec2(25, 0))) {
                setter("");
            }

            if (ImGui::BeginPopup("SelectTexPopup")) {
                auto texs = context.GetProjectAssets({".png", ".jpg", ".jpeg", ".tga"});
                if (ImGui::Selectable("None")) setter("");
                for (const auto& t : texs) {
                    if (ImGui::Selectable(std::filesystem::path(t).filename().string().c_str())) {
                        setter(t);
                    }
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
    };
}