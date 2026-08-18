#pragma once
#include "IUIWindow.h"
#include <filesystem>
#include "../Render/Material.hpp"
#include "../Render/Texture.hpp"
#include "UI/MaterialPreview.hpp"

namespace burnhope {
    class MaterialEditorWindow : public IUIWindow {
    public:
        MaterialEditorWindow() : IUIWindow("Material Editor") {}

        void Draw(UIContext& context, ui::UIWidgets& widgets, ui::Rect contentRect) override {
            if (!m_IsOpen) return;
            ui::Panel panel(widgets, m_Name, contentRect);

            context.PinMaterialFromSelection();
            const std::string& selectedPath = context.activeMaterialPath;

            if (selectedPath.empty()) {
                widgets.Text("Select a .bhmat file in Content Browser", {0.5f, 0.5f, 0.5f, 1.0f});
                return;
            }

            if (m_CurrentPath != selectedPath) {
                vkDeviceWaitIdle(context.device->device());
                m_CurrentPath = selectedPath;
                if (m_CurrentMat) context.safeDeleteQueue.push_back(m_CurrentMat);
                m_CurrentMat = Material::loadFromJson(*context.device, m_CurrentPath);
                if (!m_CurrentMat) m_CurrentMat = std::make_shared<Material>();
            }

            if (!m_CurrentMat) return;

            if (context.materialPreview) {
                context.materialPreview->UpdateEditor(*m_CurrentMat);
                widgets.ImageAt({widgets.GetCursor().x, widgets.GetCursor().y, 192.0f, 192.0f},
                                context.materialPreview->EditorView(),
                                context.materialPreview->EditorSampler(),
                                8.0f, {1, 1, 1, 1}, context.materialPreview->PreviewLayout());
                widgets.Dummy({192.0f, 196.0f});
            }

            widgets.Text("Editing: " + std::filesystem::path(m_CurrentPath).filename().string(), {0.4f, 0.7f, 1.0f, 1.0f});
            widgets.Separator();
            widgets.Text(m_CurrentMat->isPacking ? "Packing textures..." : "PBR preview / texture inputs",
                         m_CurrentMat->isPacking ? ui::Color{1.0f, 0.75f, 0.2f, 1.0f} :
                                                    ui::Color{0.5f, 0.7f, 0.9f, 1.0f});

            bool changed = false;
            changed |= widgets.DrawCheckboxControl("Repeat", &m_CurrentMat->repeatTexture);
            if (widgets.DrawCheckboxControl("Triplanar", &m_CurrentMat->useTriplanar)) {
                if (m_CurrentMat->triplanarScale < 1.0f) m_CurrentMat->triplanarScale = 1.0f;
                changed = true;
            }
            changed |= widgets.DrawVec2Control("UV Scale", m_CurrentMat->uvScale);
            if (m_CurrentMat->useTriplanar) {
                changed |= widgets.DrawFloatControl("Tri Scale", &m_CurrentMat->triplanarScale, 1.0f, 0.01f);
                if (m_CurrentMat->triplanarScale < 0.001f) m_CurrentMat->triplanarScale = 1.0f;
            }
            widgets.Separator();

            changed |= TextureSlot(widgets, context, "Albedo", m_CurrentMat->albedoPath,
                TexPtr(m_CurrentMat->albedoMap, m_CurrentMat->packedAlbedoAlpha), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setAlbedo(nullptr, "");
                else m_CurrentMat->setAlbedo(BurnhopeTexture::createTextureFromFile(*context.device, p), p);
            });
            changed |= widgets.DrawColorControl("Albedo Color", *reinterpret_cast<glm::vec3*>(&m_CurrentMat->albedoColor));
            changed |= TextureSlot(widgets, context, "Alpha / Opacity", m_CurrentMat->alphaPath, TexPtr(m_CurrentMat->alphaMap), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setAlpha(nullptr, "");
                else m_CurrentMat->setAlpha(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p);
            });
            changed |= widgets.DrawFloatControl("Opacity", &m_CurrentMat->albedoColor.w, 1.0f, 0.01f);
            changed |= widgets.DrawCheckboxControl("Transparent Material", &m_CurrentMat->isTransparent);

            changed |= TextureSlot(widgets, context, "Metallic", m_CurrentMat->metallicPath, TexPtr(m_CurrentMat->metallicMap), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setMetallic(nullptr, "");
                else { m_CurrentMat->setMetallic(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); m_CurrentMat->metallicStrength = 1.0f; }
            });
            changed |= widgets.DrawFloatControl("Metallic Strength", &m_CurrentMat->metallicStrength);

            changed |= TextureSlot(widgets, context, "Roughness", m_CurrentMat->roughnessPath, TexPtr(m_CurrentMat->roughnessMap), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setRoughness(nullptr, "");
                else { m_CurrentMat->setRoughness(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); m_CurrentMat->roughnessStrength = 1.0f; }
            });
            changed |= widgets.DrawFloatControl("Roughness Strength", &m_CurrentMat->roughnessStrength);

            changed |= TextureSlot(widgets, context, "Normal Map", m_CurrentMat->normalPath,
                TexPtr(m_CurrentMat->normalMap, m_CurrentMat->packedNormal), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setNormal(nullptr, "");
                else m_CurrentMat->setNormal(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p);
            });
            changed |= widgets.DrawFloatControl("Normal Strength", &m_CurrentMat->normalStrength);
            changed |= TextureSlot(widgets, context, "Height Map", m_CurrentMat->heightPath, TexPtr(m_CurrentMat->heightMap), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setHeight(nullptr, "");
                else m_CurrentMat->setHeight(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p);
            });
            changed |= widgets.DrawFloatControl("Height Strength", &m_CurrentMat->heightStrength);

            changed |= TextureSlot(widgets, context, "Ambient Occlusion", m_CurrentMat->aoPath, TexPtr(m_CurrentMat->aoMap), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setAO(nullptr, "");
                else { m_CurrentMat->setAO(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p); m_CurrentMat->aoStrength = 1.0f; }
            });
            changed |= TextureSlot(widgets, context, "ORM Map (AO/Rough/Metal)", m_CurrentMat->ormPath,
                TexPtr(m_CurrentMat->ormMap, m_CurrentMat->packedORMX), [&](const std::string& p) {
                if (p.empty()) {
                    m_CurrentMat->setORM(nullptr, "");
                } else {
                    m_CurrentMat->setORM(BurnhopeTexture::createDataTextureFromFile(*context.device, p), p);
                    m_CurrentMat->aoStrength = 1.0f;
                    m_CurrentMat->roughnessStrength = 1.0f;
                    m_CurrentMat->metallicStrength = 1.0f;
                }
            });

            changed |= TextureSlot(widgets, context, "Emission", m_CurrentMat->emissivePath,
                TexPtr(m_CurrentMat->emissiveMap, m_CurrentMat->packedEmissive), [&](const std::string& p) {
                if (p.empty()) m_CurrentMat->setEmissive(nullptr, "");
                else m_CurrentMat->setEmissive(BurnhopeTexture::createTextureFromFile(*context.device, p), p);
            });
            changed |= widgets.DrawColorControl("Emissive Color", m_CurrentMat->emissiveColor);
            changed |= widgets.DrawFloatControl("Emissive Intensity", &m_CurrentMat->emissiveIntensity);

            widgets.Separator();
            if (changed || widgets.Button("Save Material", {160, 30})) {
                m_CurrentMat->saveToJson(m_CurrentPath);
                if (context.materialPreview) context.materialPreview->Invalidate(m_CurrentPath);
                ReloadMaterialInScene(context, m_CurrentPath, m_CurrentMat);
                context.needsRebuild = true;
            }
        }

    private:
        static BurnhopeTexture* TexPtr(const std::shared_ptr<BurnhopeTexture>& a,
                                       const std::shared_ptr<BurnhopeTexture>& b = {}) {
            return a ? a.get() : b.get();
        }

        template <typename Setter>
        bool TextureSlot(ui::UIWidgets& widgets, UIContext& context, const std::string& label,
                         const std::string& currentPath, BurnhopeTexture* preview, Setter setter) {
            widgets.PushID(label);
            glm::vec2 row = widgets.GetCursor();
            ui::Rect thumb{row.x, row.y, 56.0f, 56.0f};
            if (preview) {
                widgets.ImageAt(thumb, preview->getImageView(), preview->getSampler(), 4.0f);
            } else {
                widgets.Background(thumb, ui::kTheme.input, 4.0f);
            }
            widgets.SetCursor({row.x + 64.0f, row.y});
            widgets.Text(label);
            widgets.SetCursor({row.x + 64.0f, row.y + 26.0f});
            std::string btnLabel = currentPath.empty() ? "None" : std::filesystem::path(currentPath).filename().string();
            bool clicked = widgets.Button(btnLabel, {200, 24});
            if (clicked) widgets.OpenPopup("TexPickerPopup");
            bool changed = false;
            ui::Rect dropRow{row.x, row.y, 264.0f, 56.0f};
            if (const auto* payload = widgets.AcceptDragDropOnRect("CONTENT_BROWSER_ITEM", dropRow)) {
                if (const std::string* path = std::any_cast<std::string>(payload)) {
                    const std::string ext = std::filesystem::path(*path).extension().string();
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga") {
                        setter(*path);
                        changed = true;
                    }
                }
            }
            if (widgets.BeginPopup("TexPickerPopup")) {
                if (widgets.Selectable("None", false)) { setter(""); changed = true; widgets.CloseCurrentPopup(); }
                for (const auto& t : context.GetProjectAssets({".png", ".jpg", ".jpeg", ".tga"})) {
                    if (widgets.Selectable(std::filesystem::path(t).filename().string(), false)) {
                        setter(t);
                        changed = true;
                        widgets.CloseCurrentPopup();
                    }
                }
                widgets.EndPopup();
            }
            widgets.SetCursor({row.x, row.y + 60.0f});
            widgets.Dummy({1.0f, 4.0f});
            widgets.PopID();
            return changed;
        }

        void ReloadMaterialInScene(UIContext& context, const std::string& path, std::shared_ptr<Material> newMat) {
            context.world->each<MeshComponent>([&](flecs::entity, MeshComponent& mc) {
                for (size_t i = 0; i < mc.materialPaths.size(); i++) {
                    if (mc.materialPaths[i] == path) {
                        if (mc.materials[i] && mc.materials[i] != newMat) context.safeDeleteQueue.push_back(mc.materials[i]);
                        mc.materials[i] = newMat;
                    }
                }
            });
        }

        std::shared_ptr<Material> m_CurrentMat;
        std::string m_CurrentPath;
    };
}
