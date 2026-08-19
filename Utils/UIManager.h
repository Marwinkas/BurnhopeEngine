#pragma once
#include <vector>
#include <memory>
#include <filesystem>
#include <vulkan/vulkan.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <SDL3/SDL.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <cstdio>

#include "../Render/Camera.hpp"
#include "UIContext.h"
#include "IUIWindow.h"
#include "UI/UI.hpp"
#include "OutlinerWindow.h"
#include "InspectorWindow.h"
#include "ContentBrowserWindow.h"
#include "PropertiesWindow.h"
#include "MaterialEditorWindow.h"

#include "UI/UIInput.hpp"
#include "UI/UIRenderer.hpp"
#include "UI/UIText.hpp"
#include "UI/UIWidgets.hpp"
#include "UI/UIDockspace.hpp"
#include "UI/NativeDialogs.hpp"
#include "UI/MaterialPreview.hpp"
#include "UI/SceneBrowserModal.hpp"

#include "Project/ProjectFile.hpp"
#include "Project/RecentFilesStore.hpp"
#include "Project/SceneController.hpp"

// New editor UI orchestrator: UIInput -> UIWidgets -> UIDockspace -> UIRenderer.
// Viewport gizmos are screen-space quads; transforms go through ApplyXform.
// Scene/project lifecycle is delegated to SceneController/ProjectFile/
// RecentFilesStore.
namespace burnhope {

    class UIManager {
    public:
        UIContext& GetContext() { return m_Context; }
        bool WantCaptureMouse() const { return m_WantCaptureMouse; }

        UIManager([[maybe_unused]] BurnhopeWindow& window, BurnhopeDevice& device, VkFormat swapChainFormat, flecs::world* world, const std::string& projectPath)
            : m_Device(&device),
              m_Recent(),
              m_Renderer(device, swapChainFormat),
              m_Text(),
              m_Widgets(m_Input, m_Renderer, m_Text, device),
              m_SceneController(m_Context, m_Project, m_Recent)
        {
            ui::NativeDialogs::Init();

            m_Context.world = world;
            m_Context.device = &device;
            try {
                m_MatPreview = std::make_unique<ui::MaterialPreview>(device);
                m_Context.materialPreview = m_MatPreview.get();
            } catch (const std::exception& e) {
                std::cerr << "MaterialPreview init failed: " << e.what() << '\n';
                m_Context.materialPreview = nullptr;
            }
            m_Context.projectDirectory = projectPath;
            m_Context.currentDirectory = projectPath;
            m_Context.dirHistory = { projectPath };
            m_Context.dirHistoryIndex = 0;

            if (!project::ProjectFile::IsProjectDirectory(projectPath)) {
                // Legacy/ad-hoc directory passed on the command line — adopt
                // it in place as a project so the new project system always
                // has a `.bhproject` to work with, without forcing a
                // migration step for the (still scene-less) existing setup.
                m_Project.projectName = std::filesystem::path(projectPath).filename().string();
                m_Project.rootDirectory = projectPath;
                std::error_code ec;
                std::filesystem::create_directories(m_Project.ScenesDir(), ec);
                std::filesystem::create_directories(m_Project.AssetsDir(), ec);
                m_Project.Save();
            } else {
                project::ProjectFile::Load(projectPath, m_Project);
            }
            m_Recent.Load();
            m_Recent.TouchProject(m_Project.rootDirectory, m_Project.projectName);

            m_Context.LoadRenderSettings(projectPath + "/rendersettings.json");

            LoadUIFont();

            m_Dockspace.RegisterPanel("Scene Outliner", ui::DockSlot::Left);
            m_Dockspace.RegisterPanel("Content Browser", ui::DockSlot::Bottom);
            m_Dockspace.RegisterPanel("Scene Inspector", ui::DockSlot::Right);
            m_Dockspace.RegisterPanel("Properties", ui::DockSlot::Right);
            m_Dockspace.RegisterPanel("Material Editor", ui::DockSlot::Right);

            m_Windows.push_back(std::make_unique<OutlinerWindow>());
            m_Windows.push_back(std::make_unique<InspectorWindow>());
            m_Windows.push_back(std::make_unique<ContentBrowserWindow>());
            m_Windows.push_back(std::make_unique<PropertiesWindow>());
            m_Windows.push_back(std::make_unique<MaterialEditorWindow>());
            for (auto& win : m_Windows) {
                win->m_IsOpen = m_Dockspace.IsPanelOpen(win->m_Name);
            }
        }

        ~UIManager() {
            vkDeviceWaitIdle(m_Device->device());
            m_Windows.clear();
            m_Context.undoStack.clear();
            m_Context.redoStack.clear();
            ui::NativeDialogs::Shutdown();
        }

        void ProcessSDLEvent(const SDL_Event& event) {
            m_Input.ProcessEvent(event);
        }

        void UpdateUI(BurnhopeWindow& window, Camera& camera, VkCommandBuffer commandBuffer) {
            m_Context.currentCommandBuffer = commandBuffer;
            auto extent = window.getExtent();

            m_Widgets.BeginFrame({(float)extent.width, (float)extent.height});
            m_Renderer.BeginFrame({(float)extent.width, (float)extent.height});
            if (!SDL_TextInputActive(window.getSDLWindow())) {
                SDL_StartTextInput(window.getSDLWindow());
            }

            ui::Rect fullRect{0, 0, (float)extent.width, (float)extent.height};

            HandleGlobalHotkeys();

            for (auto& win : m_Windows) {
                m_Dockspace.SetPanelOpen(win->m_Name, win->m_IsOpen);
            }

            DrawMainMenuBar(fullRect);
            ui::UIDockspace::Result layout = m_Dockspace.Compute(
                m_Input, m_Widgets,
                {0, ui::kMenuBarHeight, fullRect.w, fullRect.h - ui::kMenuBarHeight});

            HandleGizmosAndSelection(window, camera, layout.viewport);

            for (auto& win : m_Windows) {
                if (!win->m_IsOpen) continue;
                auto it = layout.panelContentRect.find(win->m_Name);
                if (it == layout.panelContentRect.end()) continue;
                win->Draw(m_Context, m_Widgets, it->second);
            }

            TryDropAssetOnViewport(window, camera, layout.viewport);

            if (!m_Context.requestActivateWindow.empty()) {
                m_Dockspace.ActivateTab(m_Context.requestActivateWindow);
                for (auto& win : m_Windows) {
                    if (win->m_Name == m_Context.requestActivateWindow) win->m_IsOpen = true;
                }
                m_Context.requestActivateWindow.clear();
            }

            std::string scenePickPath;
            auto sceneAction = m_SceneBrowser.Draw(m_Widgets, m_Project, m_Recent, fullRect, scenePickPath);
            if (sceneAction == ui::SceneBrowserModal::Action::Open) {
                if (scenePickPath.empty()) m_SceneController.NewScene();
                else m_SceneController.OpenScene(scenePickPath);
            } else if (sceneAction == ui::SceneBrowserModal::Action::Delete) {
                m_SceneController.DeleteScene(scenePickPath);
            }

            m_Widgets.EndFrame();

            const glm::vec2 mouse = m_Input.MousePos();
            m_LastViewport = layout.viewport;
            m_WantCaptureMouse = m_Xform.active
                || m_Widgets.IsPopupOpen()
                || m_Widgets.IsDragDropActive()
                || m_Widgets.HoveringOverlay()
                || mouse.y < kMenuBarHeight
                || !layout.viewport.Contains(mouse.x, mouse.y);

            m_Input.BeginFrame();
        }

        void RenderUI(VkCommandBuffer commandBuffer) {
            m_Renderer.Render(commandBuffer);
        }

        void ProcessPendingActions() {
            if (!m_Context.pendingSceneLoadPath.empty()) {
                m_SceneController.OpenScene(m_Context.pendingSceneLoadPath);
                m_Context.pendingSceneLoadPath = "";
            }
            if (!m_Context.pendingModelLoadPath.empty() && m_Context.pendingModelEntity.is_alive()) {
                vkDeviceWaitIdle(m_Device->device());
                if (m_Context.pendingModelEntity.has<MeshComponent>()) {
                    auto& mc = *m_Context.pendingModelEntity.get_mut<MeshComponent>();
                    m_Context.safeDeleteQueue.push_back(mc.model);
                    if (m_Context.pendingModelLoadPath == "NONE") {
                        mc.modelPath = "";
                        mc.model = nullptr;
                        mc.materialPaths.clear();
                        mc.materials.clear();
                    } else {
                        mc.modelPath = m_Context.pendingModelLoadPath;
                        mc.model = BurnhopeModel::createModelFromFile(*m_Device, mc.modelPath);
                    }
                    m_Context.needsRebuild = true;
                }
                m_Context.pendingModelLoadPath = "";
                m_Context.pendingModelEntity = flecs::entity();
            }
            if (!m_Context.pendingMatLoadPath.empty() && m_Context.pendingMatEntity.is_alive()) {
                vkDeviceWaitIdle(m_Device->device());
                if (m_Context.pendingMatEntity.has<MeshComponent>()) {
                    auto& mc = *m_Context.pendingMatEntity.get_mut<MeshComponent>();

                    if (mc.model && mc.materials.size() < mc.model->getSubMeshes().size()) {
                        mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                    }
                    if (mc.model && mc.materialPaths.size() < mc.model->getSubMeshes().size()) {
                        mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                    }

                    if (mc.model) {
                        auto newMat = (m_Context.pendingMatLoadPath == "NONE") ? nullptr : Material::loadFromJson(*m_Device, m_Context.pendingMatLoadPath);
                        const bool allSlots = m_Context.pendingMatSlot == UINT32_MAX;
                        const size_t n = std::max<size_t>(1, mc.model->getSubMeshes().size());
                        if (mc.materials.size() < n) mc.materials.resize(n, nullptr);
                        if (mc.materialPaths.size() < n) mc.materialPaths.resize(n, "");
                        for (size_t i = 0; i < n; i++) {
                            const bool hitSlot = allSlots
                                || (i < mc.model->getSubMeshes().size()
                                    && mc.model->getSubMeshes()[i].materialIndex == m_Context.pendingMatSlot)
                                || (mc.model->getSubMeshes().empty() && i == 0);
                            if (!hitSlot) continue;
                            if (mc.materials[i]) m_Context.safeDeleteQueue.push_back(mc.materials[i]);
                            mc.materialPaths[i] = (m_Context.pendingMatLoadPath == "NONE") ? "" : m_Context.pendingMatLoadPath;
                            mc.materials[i] = newMat;
                        }
                        m_Context.needsRebuild = true;
                    } else {
                        auto newMat = (m_Context.pendingMatLoadPath == "NONE") ? nullptr : Material::loadFromJson(*m_Device, m_Context.pendingMatLoadPath);
                        if (mc.materials.empty()) mc.materials.resize(1, nullptr);
                        if (mc.materialPaths.empty()) mc.materialPaths.resize(1, "");
                        if (mc.materials[0]) m_Context.safeDeleteQueue.push_back(mc.materials[0]);
                        mc.materialPaths[0] = (m_Context.pendingMatLoadPath == "NONE") ? "" : m_Context.pendingMatLoadPath;
                        mc.materials[0] = newMat;
                        m_Context.needsRebuild = true;
                    }
                }
                m_Context.pendingMatLoadPath = "";
                m_Context.pendingMatEntity = flecs::entity();
            }
        }

    private:
        static constexpr float kMenuBarHeight = ui::kMenuBarHeight;

        enum class XformOp : uint8_t { Translate, Rotate, Scale };
        enum class XformAxis : uint8_t { All, X, Y, Z };
        struct XformSnap {
            flecs::entity e;
            glm::vec3 pos{0.0f};
            glm::vec3 euler{0.0f};
            glm::vec3 scale{1.0f};
            glm::vec3 worldPos{0.0f};
            glm::quat worldRot{1.0f, 0.0f, 0.0f, 0.0f};
        };
        struct TransformSession {
            bool active = false;
            bool fromGizmo = false;
            XformOp op = XformOp::Translate;
            XformAxis axis = XformAxis::All;
            glm::vec2 accum{0.0f};
            glm::vec3 pivot{0.0f};
            glm::vec3 viewRight{1, 0, 0};
            glm::vec3 viewUp{0, 1, 0};
            glm::vec3 viewFwd{0, 0, -1};
            std::vector<XformSnap> snaps;
        };

        void LoadUIFont() {
            static const char* kCandidates[] = {
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/noto/NotoSans-Regular.ttf",
                "/usr/share/fonts/TTF/DejaVuSans.ttf",
                "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
            };
            for (const char* path : kCandidates) {
                if (std::filesystem::exists(path)) {
                    if (ui::UIFont* font = m_Text.LoadFont(path, 32)) {
                        m_Widgets.SetFont(font);
                        m_Widgets.SetFontSize(14.0f);
                        return;
                    }
                }
            }
            std::cerr << "[UIManager] No system font found — UI will render without text labels.\n";
        }

        void HandleGlobalHotkeys() {
            if (m_Input.Ctrl() && !m_Input.Shift() && m_Input.KeyPressed(SDL_SCANCODE_Z)) Undo();
            if (m_Input.Ctrl() && m_Input.Shift() && m_Input.KeyPressed(SDL_SCANCODE_Z)) Redo();

            const bool textFocus = m_Widgets.HasTextFocus();
            if (!textFocus && m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_C)
                && !m_Context.selectedEntities.empty()) {
                m_Context.CopySelection();
            }
            if (!textFocus && m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_X)
                && !m_Context.selectedEntities.empty()) {
                m_Context.SaveState();
                m_Context.CopySelection();
                auto toDelete = m_Context.selectedEntities;
                for (flecs::entity e : toDelete) {
                    if (e.is_alive() && !m_Context.HasSelectedAncestor(e))
                        m_Context.DeleteEntityRecursive(e);
                }
                m_Context.needsRebuild = true;
            }
            if (!textFocus && m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_V)
                && !m_Context.entityClipboard.empty()) {
                auto created = m_Context.PasteClipboard();
                m_Context.ClearSelection();
                for (flecs::entity c : created) {
                    if (c.is_alive() && transform::parentId(c) == 0) m_Context.ToggleSelect(c);
                }
            }

            if (m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_D) && !m_Context.selectedEntities.empty()) {
                m_Context.SaveState();
                std::vector<flecs::entity> clones;
                clones.reserve(m_Context.selectedEntities.size());
                for (flecs::entity src : m_Context.selectedEntities) {
                    if (!src.is_alive()) continue;
                    if (m_Context.HasSelectedAncestor(src)) continue;
                    clones.push_back(CloneHierarchy(src, flecs::entity()));
                }
                m_Context.ClearSelection();
                for (flecs::entity c : clones) m_Context.ToggleSelect(c);
                m_Context.needsRebuild = true;
            }

            if (m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_S)) m_SceneController.SaveScene();
            if (m_Input.Ctrl() && m_Input.KeyPressed(SDL_SCANCODE_O)) m_SceneBrowser.Open();
        }

        void DrawMainMenuBar(ui::Rect fullRect) {
            m_Widgets.Background({0, 0, fullRect.w, kMenuBarHeight}, ui::kTheme.menuBar);
            m_Widgets.Background({0, kMenuBarHeight - 1.0f, fullRect.w, 1.0f}, ui::kTheme.border);
            ui::Panel bar(m_Widgets, "##menubar", {0, 0, fullRect.w, kMenuBarHeight}, 2.0f, false);
            if (ui::MenuBar menuBar(m_Widgets); menuBar) {
                if (ui::Menu file(m_Widgets, "File"); file) {
                    if (m_Widgets.MenuItem("New Scene")) m_SceneController.NewScene();
                    if (m_Widgets.MenuItem("Save Scene", "Ctrl+S")) m_SceneController.SaveScene();
                    if (m_Widgets.MenuItem("Save Scene As...")) {
                        std::error_code ec;
                        std::filesystem::create_directories(m_Project.ScenesDir(), ec);
                        if (auto path = ui::NativeDialogs::SaveFile({{"Burnhope Scene", "bhscene"}}, m_Project.ScenesDir().string(), "Untitled.bhscene")) {
                            m_SceneController.SaveSceneAs(*path);
                        }
                    }
                    if (m_Widgets.MenuItem("Open Scene...", "Ctrl+O")) m_SceneBrowser.Open();
                    if (!m_Context.currentScenePath.empty() && m_Widgets.MenuItem("Delete Current Scene")) {
                        m_SceneController.DeleteScene(m_Context.currentScenePath);
                    }
                }
                if (ui::Menu edit(m_Widgets, "Edit"); edit) {
                    if (m_Widgets.MenuItem("Undo", "Ctrl+Z", !m_Context.undoStack.empty())) Undo();
                    if (m_Widgets.MenuItem("Redo", "Ctrl+Shift+Z", !m_Context.redoStack.empty())) Redo();
                    if (m_Widgets.MenuItem("Copy", "Ctrl+C", !m_Context.selectedEntities.empty()))
                        m_Context.CopySelection();
                    if (m_Widgets.MenuItem("Cut", "Ctrl+X", !m_Context.selectedEntities.empty())) {
                        m_Context.SaveState();
                        m_Context.CopySelection();
                        auto toDelete = m_Context.selectedEntities;
                        for (flecs::entity e : toDelete) {
                            if (e.is_alive() && !m_Context.HasSelectedAncestor(e))
                                m_Context.DeleteEntityRecursive(e);
                        }
                        m_Context.needsRebuild = true;
                    }
                    if (m_Widgets.MenuItem("Paste", "Ctrl+V", !m_Context.entityClipboard.empty())) {
                        auto created = m_Context.PasteClipboard();
                        m_Context.ClearSelection();
                        for (flecs::entity c : created) {
                            if (c.is_alive() && transform::parentId(c) == 0) m_Context.ToggleSelect(c);
                        }
                    }
                }
                if (ui::Menu window(m_Widgets, "Window"); window) {
                    for (auto& win : m_Windows) {
                        if (m_Widgets.MenuItem(win->m_Name, "", true, win->m_IsOpen)) {
                            win->m_IsOpen = !win->m_IsOpen;
                            if (win->m_IsOpen) m_Dockspace.ActivateTab(win->m_Name);
                        }
                    }
                    if (m_Widgets.MenuItem("Reset Layout")) {
                        m_Dockspace.ResetLayout();
                        for (auto& win : m_Windows) win->m_IsOpen = true;
                    }
                }
            }
            DrawViewportToolbar(fullRect);
        }

        void HandleGizmosAndSelection(BurnhopeWindow& window, Camera& camera, ui::Rect viewport) {
            m_Context.PruneSelection();

            const bool looking = m_Input.MouseDown(2);
            const bool uiBusy = m_Widgets.IsPopupOpen() || m_Widgets.IsDragDropActive() || m_Widgets.HasTextFocus();
            if (!uiBusy && !m_Xform.active && !looking && m_Context.selectedEntity.is_alive()) {
                if (m_Input.KeyPressed(SDL_SCANCODE_G)) BeginXform(XformOp::Translate, false, camera);
                if (m_Input.KeyPressed(SDL_SCANCODE_R)) BeginXform(XformOp::Rotate, false, camera);
                if (m_Input.KeyPressed(SDL_SCANCODE_S) && !m_Input.Ctrl()) BeginXform(XformOp::Scale, false, camera);
            }

            flecs::entity lightHover = flecs::entity();
            const bool iconHit = DrawLightIcons(window, camera, viewport, lightHover);

            XformAxis gizmoHitAxis = XformAxis::All;
            bool gizmoHit = false;
            if (m_Context.selectedEntity.is_alive() && transform::hasBundle(m_Context.selectedEntity)) {
                gizmoHit = DrawTransformGizmo(camera, viewport, gizmoHitAxis);
            }

            if (m_Xform.active) {
                if (m_Input.KeyPressed(SDL_SCANCODE_X)) ToggleXformAxis(XformAxis::X);
                if (m_Input.KeyPressed(SDL_SCANCODE_Y)) ToggleXformAxis(XformAxis::Y);
                if (m_Input.KeyPressed(SDL_SCANCODE_Z)) ToggleXformAxis(XformAxis::Z);
                if (m_Xform.axis != XformAxis::All)
                    DrawConstraintAxis(camera, viewport, m_Xform.op, m_Xform.axis);

                m_Xform.accum += m_Input.MouseDelta();
                WrapMouse(window);
                ApplyXform(camera);

                const bool cancel = m_Input.MouseRightClicked() || m_Input.KeyPressed(SDL_SCANCODE_ESCAPE);
                const bool confirm = m_Xform.fromGizmo
                    ? m_Input.MouseReleased(0)
                    : (m_Input.MouseClicked(0) || m_Input.KeyPressed(SDL_SCANCODE_RETURN));
                if (cancel) CancelXform();
                else if (confirm) EndXform(true);
            } else if (!uiBusy && !looking && gizmoHit && m_Input.MouseClicked(0) && !iconHit) {
                BeginXform(m_GizmoOp, true, camera);
                m_Xform.axis = gizmoHitAxis;
            }

            if (!m_Xform.active && !gizmoHit && !uiBusy
                && viewport.Contains(m_Input.MousePos().x, m_Input.MousePos().y)) {
                flecs::entity hover = lightHover;
                if (!hover.is_alive() && !iconHit) hover = PickEntityUnderMouse(window, camera, viewport);
                if (hover.is_alive()) DrawEntityHover(hover, camera, viewport);

                if (m_Input.MouseClicked(0) && !iconHit) {
                    flecs::entity hit = hover.is_alive() ? hover : PickEntityUnderMouse(window, camera, viewport);
                    if (hit.is_alive()) {
                        if (m_Input.Ctrl()) m_Context.ToggleSelect(hit);
                        else m_Context.SelectOnly(hit);
                    } else if (!m_Input.Ctrl()) {
                        m_Context.ClearSelection();
                    }
                }
            }
        }

        bool ToolBtn(std::string_view label, bool active, glm::vec2 size) {
            glm::vec2 c = m_Widgets.GetCursor();
            ui::Rect r{c.x, c.y, size.x, size.y};
            bool clicked = m_Widgets.InvisibleHit(label, r);
            ui::Color bg = active ? ui::kTheme.buttonActive
                         : (m_Widgets.IsMouseOverItem() ? ui::kTheme.buttonHover : ui::kTheme.button);
            m_Widgets.Background(r, bg, 3.0f);
            m_Widgets.SetCursor({c.x, c.y});
            m_Widgets.TextClippedCentered(label, size.x, ui::kTheme.text);
            m_Widgets.SetCursor({c.x + size.x + 3.0f, c.y});
            return clicked;
        }

        float CurrentSnap() const {
            switch (m_GizmoOp) {
                case XformOp::Rotate: return m_SnapRotate;
                case XformOp::Scale: return m_SnapScale;
                default: return m_SnapTranslate;
            }
        }

        void DrawSnapPresets(float* value, const float* presets, int count) {
            glm::vec2 row = m_Widgets.GetCursor();
            float x = row.x;
            for (int i = 0; i < count; ++i) {
                char buf[24];
                if (presets[i] == 0.0f) std::snprintf(buf, sizeof(buf), "Off");
                else if (presets[i] < 1.0f) std::snprintf(buf, sizeof(buf), "%.2g", presets[i]);
                else std::snprintf(buf, sizeof(buf), "%g", presets[i]);
                m_Widgets.PushIDInt(static_cast<uint64_t>(i + 1));
                m_Widgets.SetCursor({x, row.y});
                bool on = std::abs(*value - presets[i]) < 0.0001f;
                if (ToolBtn(buf, on, {42.0f, 22.0f})) *value = presets[i];
                m_Widgets.PopID();
                x += 45.0f;
                if (i == 4) {
                    x = row.x;
                    row.y += 24.0f;
                }
            }
            m_Widgets.SetCursor({row.x, row.y + 26.0f});
        }

        void DrawViewportToolbar(ui::Rect fullRect) {
            const float h = 22.0f;
            const float toolW = 610.0f;
            m_Widgets.SetCursor({std::max(210.0f, fullRect.w - toolW), 3.0f});
            if (ToolBtn("Move", m_GizmoOp == XformOp::Translate, {48, h}))
                m_GizmoOp = XformOp::Translate;
            if (ToolBtn("Rotate", m_GizmoOp == XformOp::Rotate, {56, h}))
                m_GizmoOp = XformOp::Rotate;
            if (ToolBtn("Scale", m_GizmoOp == XformOp::Scale, {50, h}))
                m_GizmoOp = XformOp::Scale;

            m_Widgets.SetCursor({m_Widgets.GetCursor().x + 6.0f, 3.0f});
            if (ToolBtn("World", !m_GizmoLocal, {52, h})) m_GizmoLocal = false;
            if (ToolBtn("Local", m_GizmoLocal, {50, h})) m_GizmoLocal = true;

            m_Widgets.SetCursor({m_Widgets.GetCursor().x + 6.0f, 3.0f});
            if (ToolBtn("Origin", !m_PivotCenter, {56, h})) m_PivotCenter = false;
            if (ToolBtn("Center", m_PivotCenter, {56, h})) m_PivotCenter = true;

            m_Widgets.SetCursor({m_Widgets.GetCursor().x + 6.0f, 3.0f});
            char snapLabel[32];
            float cur = CurrentSnap();
            if (cur <= 0.0001f) std::snprintf(snapLabel, sizeof(snapLabel), "Snap");
            else std::snprintf(snapLabel, sizeof(snapLabel), "Snap %g", cur);
            if (ToolBtn(snapLabel, cur > 0.0001f, {72, h})) m_Widgets.OpenPopup("GizmoSnap");

            if (m_Widgets.BeginPopup("GizmoSnap", 470.0f)) {
                m_Widgets.Text("Move (units)", ui::kTheme.textMuted);
                m_Widgets.DrawFloatControl("Move", &m_SnapTranslate, 1.0f, 0.05f);
                const float moveP[10] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 25.0f};
                DrawSnapPresets(&m_SnapTranslate, moveP, 10);

                m_Widgets.Text("Rotate (degrees)", ui::kTheme.textMuted);
                m_Widgets.DrawFloatControl("Rotate", &m_SnapRotate, 15.0f, 0.5f);
                const float rotP[10] = {1.0f, 5.0f, 10.0f, 15.0f, 25.0f, 30.0f, 45.0f, 90.0f, 180.0f, 0.0f};
                DrawSnapPresets(&m_SnapRotate, rotP, 10);

                m_Widgets.Text("Scale", ui::kTheme.textMuted);
                m_Widgets.DrawFloatControl("Scale", &m_SnapScale, 0.1f, 0.01f);
                const float scP[10] = {0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 0.0f};
                DrawSnapPresets(&m_SnapScale, scP, 10);
                m_Widgets.EndPopup();
            }
        }

        bool ProjectToScreen(const Camera& camera, const glm::vec3& world, glm::vec2& out) const {
            glm::mat4 vp = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f) * camera.GetViewMatrix();
            glm::vec4 clip = vp * glm::vec4(world, 1.0f);
            if (clip.w <= 0.001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float w = camera.width > 0 ? (float)camera.width : 1.0f;
            float h = camera.height > 0 ? (float)camera.height : 1.0f;
            out.x = (ndc.x * 0.5f + 0.5f) * w;
            out.y = (ndc.y * 0.5f + 0.5f) * h;
            return true;
        }

        bool WorldToScreen(const Camera& camera, const glm::vec3& world, glm::vec2& out) const {
            glm::mat4 vp = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f) * camera.GetViewMatrix();
            glm::vec4 clip = vp * glm::vec4(world, 1.0f);
            if (clip.w <= 0.001f) return false;
            glm::vec3 ndc = glm::vec3(clip) / clip.w;
            float w = camera.width > 0 ? (float)camera.width : 1.0f;
            float h = camera.height > 0 ? (float)camera.height : 1.0f;
            out.x = (ndc.x * 0.5f + 0.5f) * w;
            out.y = (ndc.y * 0.5f + 0.5f) * h;
            return ndc.z >= 0.0f && ndc.z <= 1.0f;
        }

        static bool ClipLineToRect(glm::vec2& a, glm::vec2& b, ui::Rect r) {
            const float x0 = a.x, y0 = a.y;
            const float dx = b.x - a.x, dy = b.y - a.y;
            float t0 = 0.0f, t1 = 1.0f;
            auto clip = [&](float p, float q) {
                if (std::abs(p) < 1e-8f) return q >= 0.0f;
                const float t = q / p;
                if (p < 0.0f) {
                    if (t > t1) return false;
                    if (t > t0) t0 = t;
                } else {
                    if (t < t0) return false;
                    if (t < t1) t1 = t;
                }
                return true;
            };
            if (!clip(-dx, x0 - r.x)) return false;
            if (!clip(dx, r.x + r.w - x0)) return false;
            if (!clip(-dy, y0 - r.y)) return false;
            if (!clip(dy, r.y + r.h - y0)) return false;
            a = {x0 + t0 * dx, y0 + t0 * dy};
            b = {x0 + t1 * dx, y0 + t1 * dy};
            return true;
        }

        bool ProjectWorldLine(const Camera& camera, glm::vec3 wa, glm::vec3 wb, glm::vec2& sa, glm::vec2& sb) const {
            bool ha = ProjectToScreen(camera, wa, sa);
            bool hb = ProjectToScreen(camera, wb, sb);
            if (ha && hb) return true;
            if (!ha && !hb) return false;
            glm::vec3 vis = ha ? wa : wb;
            glm::vec3 hid = ha ? wb : wa;
            for (int i = 0; i < 24; ++i) {
                glm::vec3 mid = (vis + hid) * 0.5f;
                glm::vec2 sm;
                if (ProjectToScreen(camera, mid, sm)) vis = mid;
                else hid = mid;
            }
            if (ha) return ProjectToScreen(camera, vis, sb);
            return ProjectToScreen(camera, vis, sa);
        }

        void DrawScreenDot(glm::vec2 p, float size, ui::Color color, ui::Rect viewport) {
            ui::Rect r{p.x - size * 0.5f, p.y - size * 0.5f, size, size};
            r = r.Intersect(viewport);
            if (r.w > 0.4f && r.h > 0.4f) m_Widgets.Background(r, color);
        }

        void DrawScreenLine(glm::vec2 a, glm::vec2 b, float thickness, ui::Color color, ui::Rect viewport) {
            if (!ClipLineToRect(a, b, viewport)) return;
            glm::vec2 d = b - a;
            float len = glm::length(d);
            if (len < 0.5f) {
                DrawScreenDot(a, thickness, color, viewport);
                return;
            }
            glm::vec2 n = d / len;
            const float step = std::max(1.4f, thickness * 0.8f);
            for (float t = 0.0f; t <= len; t += step)
                DrawScreenDot(a + n * t, thickness, color, viewport);
        }

        static ui::Color AxisColor(int i, bool hot) {
            ui::Color c = (i == 0) ? ui::kTheme.axisX : (i == 1) ? ui::kTheme.axisY : ui::kTheme.axisZ;
            if (!hot) return c;
            return {std::min(1.0f, c.r * 1.35f + 0.2f),
                    std::min(1.0f, c.g * 1.35f + 0.2f),
                    std::min(1.0f, c.b * 1.35f + 0.2f), 1.0f};
        }

        static float DistToSeg(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
            glm::vec2 ab = b - a;
            float l2 = glm::dot(ab, ab);
            if (l2 < 1e-6f) return glm::length(p - a);
            float t = glm::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f);
            return glm::length(p - (a + ab * t));
        }

        glm::mat3 GizmoAxes(flecs::entity entity) {
            if (m_GizmoLocal) return glm::mat3(GetGlobalRotation(entity));
            return glm::mat3(1.0f);
        }

        glm::vec3 GizmoPivot(flecs::entity entity) {
            if (m_Xform.active && m_Xform.op != XformOp::Translate) return m_Xform.pivot;
            return EntityPivotWorld(entity);
        }

        void DrawConstraintAxis(const Camera& camera, ui::Rect viewport, XformOp op, XformAxis axis) {
            if (axis == XformAxis::All) return;
            const int i = static_cast<int>(axis) - 1;
            glm::vec3 dir = XformAxisWorld();
            if (glm::length(dir) < 1e-6f) return;
            dir = glm::normalize(dir);
            glm::vec3 origin = m_Xform.pivot;
            if (op == XformOp::Translate && m_Context.selectedEntity.is_alive())
                origin = EntityPivotWorld(m_Context.selectedEntity);
            constexpr float kInf = 5000.0f;
            glm::vec3 a = (op == XformOp::Translate) ? origin : (origin - dir * kInf);
            glm::vec3 b = origin + dir * kInf;
            glm::vec2 sa, sb;
            if (!ProjectWorldLine(camera, a, b, sa, sb)) return;
            DrawScreenLine(sa, sb, 3.0f, AxisColor(i, true), viewport);
        }

        bool DrawTransformGizmo(const Camera& camera, ui::Rect viewport, XformAxis& outAxis) {
            outAxis = XformAxis::All;
            flecs::entity entity = m_Context.selectedEntity;
            if (!entity.is_alive() || !transform::hasBundle(entity)) return false;

            glm::vec3 pivot = GizmoPivot(entity);
            glm::mat3 axes = (m_Xform.active && m_GizmoLocal && !m_Xform.snaps.empty())
                ? glm::mat3_cast(m_Xform.snaps.front().worldRot)
                : GizmoAxes(entity);

            glm::vec2 origin;
            if (!WorldToScreen(camera, pivot, origin)) return false;

            const XformOp op = m_Xform.active ? m_Xform.op : m_GizmoOp;
            const glm::vec2 mouse = m_Input.MousePos();
            const bool mouseIn = viewport.Contains(mouse.x, mouse.y);
            const float dist = std::max(0.15f, glm::length(camera.Position - pivot));
            const float worldLen = dist * 0.15f;
            const float handlePx = 80.0f;
            const float hitPad = 12.0f;
            const int segs = 48;
            const float worldR = dist * 0.11f;

            glm::vec2 axisEnd[3]{};
            bool axisOk[3] = {false, false, false};
            glm::vec2 ringPt[3][49]{};
            bool ringVis[3][49]{};
            int ringCount[3] = {0, 0, 0};

            float best = hitPad;
            int hit = -1;

            if (op == XformOp::Rotate) {
                for (int i = 0; i < 3; ++i) {
                    glm::vec3 n = axes[i];
                    float nlen = glm::length(n);
                    if (nlen < 1e-6f) continue;
                    n /= nlen;
                    glm::vec3 ref = (std::abs(n.y) < 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    glm::vec3 u = glm::normalize(glm::cross(n, ref));
                    glm::vec3 v = glm::cross(n, u);
                    glm::vec2 prev{};
                    bool prevOk = false;
                    float minD = 1.0e8f;
                    for (int s = 0; s <= segs; ++s) {
                        float ang = (static_cast<float>(s) / static_cast<float>(segs)) * 6.2831853f;
                        glm::vec3 p = pivot + (u * std::cos(ang) + v * std::sin(ang)) * worldR;
                        glm::vec2 sp;
                        const bool vis = WorldToScreen(camera, p, sp);
                        const int idx = ringCount[i]++;
                        ringPt[i][idx] = sp;
                        ringVis[i][idx] = vis;
                        if (vis && prevOk) minD = std::min(minD, DistToSeg(mouse, prev, sp));
                        prev = sp;
                        prevOk = vis;
                    }
                    if (mouseIn && minD < best) { best = minD; hit = i; }
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    glm::vec2 ep;
                    if (!WorldToScreen(camera, pivot + axes[i] * worldLen, ep)) continue;
                    glm::vec2 dir = ep - origin;
                    float len = glm::length(dir);
                    if (len < 4.0f) continue;
                    axisEnd[i] = origin + (dir / len) * handlePx;
                    axisOk[i] = true;
                    if (mouseIn) {
                        float d = DistToSeg(mouse, origin, axisEnd[i]);
                        if (d < best) { best = d; hit = i; }
                    }
                }
                const float centerSize = (op == XformOp::Scale) ? 12.0f : 8.0f;
                if (mouseIn) {
                    float cd = glm::length(mouse - origin);
                    if (cd < centerSize + 4.0f && cd < best) { best = cd; hit = -2; }
                }
            }

            const XformAxis hover = (hit == -2) ? XformAxis::All
                : (hit >= 0) ? static_cast<XformAxis>(hit + 1) : XformAxis::All;
            const XformAxis hl = m_Xform.active ? m_Xform.axis : hover;

            if (op == XformOp::Rotate) {
                for (int i = 0; i < 3; ++i) {
                    const bool hot = hl == static_cast<XformAxis>(i + 1);
                    ui::Color col = AxisColor(i, hot);
                    for (int s = 1; s < ringCount[i]; ++s) {
                        if (ringVis[i][s - 1] && ringVis[i][s])
                            DrawScreenLine(ringPt[i][s - 1], ringPt[i][s], hot ? 3.4f : 2.2f, col, viewport);
                    }
                }
            } else {
                for (int i = 0; i < 3; ++i) {
                    if (!axisOk[i]) continue;
                    const bool hot = hl == static_cast<XformAxis>(i + 1)
                        || (hl == XformAxis::All && hit == -2 && op == XformOp::Scale);
                    ui::Color col = AxisColor(i, hot);
                    glm::vec2 dir = axisEnd[i] - origin;
                    float len = glm::length(dir);
                    if (len < 1.0f) continue;
                    dir /= len;
                    DrawScreenLine(origin, axisEnd[i], hot ? 3.6f : 2.6f, col, viewport);
                    if (op == XformOp::Translate) {
                        glm::vec2 perp{-dir.y, dir.x};
                        DrawScreenLine(axisEnd[i], axisEnd[i] - dir * 12.0f + perp * 6.0f, 2.4f, col, viewport);
                        DrawScreenLine(axisEnd[i], axisEnd[i] - dir * 12.0f - perp * 6.0f, 2.4f, col, viewport);
                    } else {
                        DrawScreenDot(axisEnd[i], hot ? 11.0f : 9.0f, col, viewport);
                    }
                }
                const float centerSize = (op == XformOp::Scale) ? 12.0f : 8.0f;
                const bool centerHot = hit == -2 || (m_Xform.active && m_Xform.axis == XformAxis::All);
                DrawScreenDot(origin, centerHot ? centerSize + 2.0f : centerSize,
                              centerHot ? ui::Color::RGBA8(255, 255, 255) : ui::Color::RGBA8(230, 230, 234),
                              viewport);
            }

            if (hit == -1) return false;
            outAxis = hover;
            return true;
        }

        bool DrawLightIcons(BurnhopeWindow& window, Camera& camera, ui::Rect viewport, flecs::entity& outHover) {
            (void)window;
            bool clickedIcon = false;
            outHover = flecs::entity();
            m_Context.world->each<LightComponent>([&](flecs::entity entity, LightComponent&) {
                if (!transform::hasBundle(entity)) return;
                glm::vec3 pos = glm::vec3(GetGlobalTransform(entity)[3]);
                glm::vec2 sp;
                if (!WorldToScreen(camera, pos, sp)) return;
                const float s = 22.0f;
                ui::Rect icon{sp.x - s * 0.5f, sp.y - s * 0.5f, s, s};
                if (!viewport.Overlaps(icon)) return;

                m_Widgets.PushIDInt(entity.id());
                bool hit = m_Widgets.InvisibleHit("##light", icon);
                bool hover = m_Widgets.IsMouseOverItem();
                bool selected = m_Context.IsSelected(entity);
                ui::Color fill = selected ? ui::Color::RGBA8(255, 210, 70) : ui::Color::RGBA8(255, 186, 40);
                if (hover) fill = ui::Color::RGBA8(255, 230, 120);
                m_Widgets.Background({icon.x + 4, icon.y + 4, 14, 14}, fill, 7.0f);
                m_Widgets.Background({icon.x + 9, icon.y + 1, 4, 4}, fill, 1.0f);
                m_Widgets.Background({icon.x + 9, icon.y + 17, 4, 4}, fill, 1.0f);
                m_Widgets.Background({icon.x + 1, icon.y + 9, 4, 4}, fill, 1.0f);
                m_Widgets.Background({icon.x + 17, icon.y + 9, 4, 4}, fill, 1.0f);
                if (selected) m_Widgets.Background(icon, ui::Color::RGBA8(56, 132, 230, 70), 11.0f);
                if (hover) {
                    outHover = entity;
                    m_Widgets.SetTooltip(EntityDisplayName(entity));
                }
                if (hit) {
                    clickedIcon = true;
                    if (m_Input.Ctrl()) m_Context.ToggleSelect(entity);
                    else m_Context.SelectOnly(entity);
                }
                m_Widgets.PopID();
            });
            return clickedIcon;
        }

        glm::vec3 EntityPivotWorld(flecs::entity entity) {
            glm::mat4 world = GetGlobalTransform(entity);
            glm::vec3 origin = glm::vec3(world[3]);
            if (!m_PivotCenter || !entity.has<MeshComponent>()) return origin;
            glm::vec3 bmin, bmax;
            EntityLocalAabb(entity, bmin, bmax);
            glm::vec3 localCenter = (bmin + bmax) * 0.5f;
            return glm::vec3(world * glm::vec4(localCenter, 1.0f));
        }

        glm::mat4 GetGlobalRotation(flecs::entity entity) {
            if (!entity.is_alive() || !entity.has<RotationEuler>()) return glm::mat4(1.0f);
            glm::mat4 r = transform::rotationMatrix(*entity.get<RotationEuler>());
            if (entity.has<HierarchyComponent>()) {
                uint64_t pid = entity.get<HierarchyComponent>()->parentID;
                flecs::entity parent = m_Context.FindEntityByID(pid);
                if (parent.is_alive()) r = GetGlobalRotation(parent) * r;
            }
            return r;
        }

        static glm::vec3 MatrixEulerDeg(const glm::mat4& m) {
            glm::vec3 sx = glm::vec3(m[0]);
            glm::vec3 sy = glm::vec3(m[1]);
            glm::vec3 sz = glm::vec3(m[2]);
            float lx = glm::length(sx), ly = glm::length(sy), lz = glm::length(sz);
            glm::mat3 R(
                lx > 1e-8f ? sx / lx : glm::vec3(1, 0, 0),
                ly > 1e-8f ? sy / ly : glm::vec3(0, 1, 0),
                lz > 1e-8f ? sz / lz : glm::vec3(0, 0, 1));
            // Same Rx*Ry*Rz convention as transform::rotationMatrix, in degrees.
            float x = glm::degrees(std::atan2(R[1][2], R[2][2]));
            float y = glm::degrees(std::atan2(-R[0][2], std::sqrt(R[1][2] * R[1][2] + R[2][2] * R[2][2])));
            float z = glm::degrees(std::atan2(R[0][1], R[0][0]));
            return {x, y, z};
        }

        static void ContinuizeEuler(glm::vec3& euler, const glm::vec3& previous) {
            for (int i = 0; i < 3; ++i) {
                while (euler[i] - previous[i] > 180.0f) euler[i] -= 360.0f;
                while (euler[i] - previous[i] < -180.0f) euler[i] += 360.0f;
            }
        }

        static glm::quat EulerToQuat(const glm::vec3& e) {
            return glm::angleAxis(glm::radians(e.x), glm::vec3(1, 0, 0))
                 * glm::angleAxis(glm::radians(e.y), glm::vec3(0, 1, 0))
                 * glm::angleAxis(glm::radians(e.z), glm::vec3(0, 0, 1));
        }

        static float SnapF(float v, float s) {
            if (s <= 1e-8f) return v;
            return std::round(v / s) * s;
        }

        std::string EntityDisplayName(flecs::entity e) const {
            if (e.is_alive() && e.has<TagComponent>()) return e.get<TagComponent>()->name;
            if (e.is_alive() && e.has<LightComponent>()) return "Light";
            return "Object";
        }

        glm::mat4 ParentWorldMatrix(flecs::entity entity) {
            if (!entity.is_alive() || !entity.has<HierarchyComponent>()) return glm::mat4(1.0f);
            uint64_t pid = entity.get<HierarchyComponent>()->parentID;
            flecs::entity p = m_Context.FindEntityByID(pid);
            if (p.is_alive()) return GetGlobalTransform(p);
            return glm::mat4(1.0f);
        }

        glm::vec3 WorldToLocalPos(flecs::entity entity, const glm::vec3& worldPos) {
            return glm::vec3(glm::inverse(ParentWorldMatrix(entity)) * glm::vec4(worldPos, 1.0f));
        }

        glm::vec3 WorldQuatToLocalEuler(flecs::entity entity, const glm::quat& worldQ, const glm::vec3& startEuler) {
            glm::mat4 parentR(1.0f);
            if (entity.has<HierarchyComponent>()) {
                uint64_t pid = entity.get<HierarchyComponent>()->parentID;
                flecs::entity p = m_Context.FindEntityByID(pid);
                if (p.is_alive()) parentR = GetGlobalRotation(p);
            }
            glm::quat localQ = glm::inverse(glm::quat_cast(parentR)) * worldQ;
            glm::vec3 e = MatrixEulerDeg(glm::mat4_cast(localQ));
            ContinuizeEuler(e, startEuler);
            return e;
        }

        glm::vec3 XformAxisWorld() {
            if (m_Xform.axis == XformAxis::All) return glm::vec3(0.0f);
            int i = static_cast<int>(m_Xform.axis) - 1;
            glm::vec3 a(0.0f); a[i] = 1.0f;
            if (m_GizmoLocal && !m_Xform.snaps.empty())
                a = glm::mat3_cast(m_Xform.snaps.front().worldRot) * a;
            float len = glm::length(a);
            return len > 1e-8f ? a / len : a;
        }

        float ActiveSnap() const {
            if (m_Input.Ctrl() && m_Input.Shift()) return 0.1f;
            if (m_Input.Ctrl()) return 1.0f;
            return CurrentSnap();
        }

        void BeginXform(XformOp op, bool fromGizmo, const Camera& camera) {
            if (m_Context.selectedEntities.empty() || !m_Context.selectedEntity.is_alive()) return;
            if (!m_Xform.active) m_Context.SaveState();
            m_Xform.active = true;
            m_Xform.fromGizmo = fromGizmo;
            m_Xform.op = op;
            m_Xform.axis = XformAxis::All;
            m_Xform.accum = {0.0f, 0.0f};
            m_Xform.pivot = EntityPivotWorld(m_Context.selectedEntity);
            glm::vec3 fwd = glm::normalize(camera.Orientation);
            m_Xform.viewRight = glm::normalize(glm::cross(fwd, camera.Up));
            m_Xform.viewUp = glm::normalize(glm::cross(m_Xform.viewRight, fwd));
            m_Xform.viewFwd = fwd;
            m_Xform.snaps.clear();
            m_Xform.snaps.reserve(m_Context.selectedEntities.size());
            for (flecs::entity e : m_Context.selectedEntities) {
                if (!e.is_alive() || !transform::hasBundle(e)) continue;
                if (SelectionHasSelectedAncestor(e)) continue;
                XformSnap s;
                s.e = e;
                s.pos = transform::asVec3(*e.get<Position3>());
                s.euler = transform::asVec3(*e.get<RotationEuler>());
                s.scale = transform::asVec3(*e.get<Scale3>());
                s.worldPos = glm::vec3(GetGlobalTransform(e)[3]);
                s.worldRot = glm::quat_cast(GetGlobalRotation(e));
                m_Xform.snaps.push_back(s);
            }
            m_GizmoOp = op;
        }

        void ToggleXformAxis(XformAxis axis) {
            m_Xform.axis = (m_Xform.axis == axis) ? XformAxis::All : axis;
        }

        void RestoreXformSnaps() {
            for (const XformSnap& s : m_Xform.snaps) {
                if (!s.e.is_alive() || !transform::hasBundle(s.e)) continue;
                transform::asVec3Mut(*s.e.get_mut<Position3>()) = s.pos;
                transform::asVec3Mut(*s.e.get_mut<RotationEuler>()) = s.euler;
                transform::asVec3Mut(*s.e.get_mut<Scale3>()) = s.scale;
                transform::markDirty(*s.e.get_mut<LocalMatrix>());
            }
        }

        void CancelXform() {
            RestoreXformSnaps();
            if (!m_Context.undoStack.empty()) m_Context.undoStack.pop_back();
            m_Xform = {};
            m_Context.needsRebuild = true;
            m_Context.needsRTRebuild = true;
        }

        void EndXform(bool /*commit*/) {
            m_Context.needsRebuild = true;
            m_Context.needsRTRebuild = true;
            m_Xform = {};
        }

        void WriteSnapTransform(const XformSnap& s, const glm::vec3& pos, const glm::vec3& euler, const glm::vec3& scale) {
            if (!s.e.is_alive() || !transform::hasBundle(s.e)) return;
            transform::asVec3Mut(*s.e.get_mut<Position3>()) = pos;
            transform::asVec3Mut(*s.e.get_mut<RotationEuler>()) = euler;
            transform::asVec3Mut(*s.e.get_mut<Scale3>()) = scale;
            transform::markDirty(*s.e.get_mut<LocalMatrix>());
        }

        void WrapMouse(BurnhopeWindow& window) {
            auto ext = window.getExtent();
            glm::vec2 p = m_Input.MousePos();
            glm::vec2 np = p;
            const float m = 6.0f;
            if (p.x <= m) np.x = (float)ext.width - m - 2.0f;
            else if (p.x >= (float)ext.width - m) np.x = m + 2.0f;
            if (p.y <= m) np.y = (float)ext.height - m - 2.0f;
            else if (p.y >= (float)ext.height - m) np.y = m + 2.0f;
            if (np.x != p.x || np.y != p.y) {
                m_Input.SkipNextMouseDelta();
                m_Input.SetMousePos(np);
                SDL_WarpMouseInWindow(window.getSDLWindow(), np.x, np.y);
            }
        }

        void ApplyXform(const Camera& camera) {
            const float snap = ActiveSnap();
            const glm::vec2 d = m_Xform.accum;
            const float dist = std::max(0.15f, glm::length(camera.Position - m_Xform.pivot));

            float axisAlong = 0.0f;
            glm::vec3 axisDir(0.0f);
            if (m_Xform.axis != XformAxis::All) {
                axisDir = XformAxisWorld();
                glm::vec2 sp, ep;
                if (glm::length(axisDir) > 1e-6f
                    && WorldToScreen(camera, m_Xform.pivot, sp)
                    && WorldToScreen(camera, m_Xform.pivot + axisDir, ep)) {
                    glm::vec2 sa = ep - sp;
                    float sl = glm::length(sa);
                    if (sl > 1.0f) axisAlong = glm::dot(d, sa / sl);
                    else {
                        glm::vec3 move = m_Xform.viewRight * (d.x * dist * 0.0025f)
                                       + m_Xform.viewUp * (-d.y * dist * 0.0025f);
                        axisAlong = glm::dot(move, axisDir) / std::max(1e-6f, dist * 0.0025f);
                    }
                } else {
                    glm::vec3 move = m_Xform.viewRight * (d.x * dist * 0.0025f)
                                   + m_Xform.viewUp * (-d.y * dist * 0.0025f);
                    axisAlong = glm::dot(move, axisDir) / std::max(1e-6f, dist * 0.0025f);
                }
            }

            for (const XformSnap& s : m_Xform.snaps) {
                if (!s.e.is_alive() || !transform::hasBundle(s.e)) continue;
                glm::vec3 pos = s.pos;
                glm::vec3 euler = s.euler;
                glm::vec3 scale = s.scale;

                if (m_Xform.op == XformOp::Translate) {
                    glm::vec3 move;
                    if (m_Xform.axis != XformAxis::All) {
                        float along = axisAlong * dist * 0.0025f;
                        if (snap > 0.0f) along = SnapF(along, snap);
                        move = axisDir * along;
                    } else {
                        move = m_Xform.viewRight * (d.x * dist * 0.0025f)
                             + m_Xform.viewUp * (-d.y * dist * 0.0025f);
                        if (snap > 0.0f)
                            move = {SnapF(move.x, snap), SnapF(move.y, snap), SnapF(move.z, snap)};
                    }
                    pos = WorldToLocalPos(s.e, s.worldPos + move);
                } else if (m_Xform.op == XformOp::Rotate) {
                    float deg = d.x * 0.35f;
                    glm::vec3 axis = (m_Xform.axis == XformAxis::All) ? m_Xform.viewFwd : axisDir;
                    if (glm::length(axis) < 1e-6f) axis = m_Xform.viewFwd;
                    axis = glm::normalize(axis);
                    if (snap > 0.0f) deg = SnapF(deg, snap);
                    glm::quat dq = glm::angleAxis(glm::radians(deg), axis);
                    glm::quat newWorldR = dq * s.worldRot;
                    glm::vec3 rel = s.worldPos - m_Xform.pivot;
                    glm::vec3 newWorldP = m_Xform.pivot + dq * rel;
                    pos = WorldToLocalPos(s.e, newWorldP);
                    euler = WorldQuatToLocalEuler(s.e, newWorldR, s.euler);
                } else {
                    float add = (m_Xform.axis == XformAxis::All) ? (d.x * 0.01f) : (axisAlong * 0.01f);
                    if (snap > 0.0f) add = SnapF(add, snap);
                    if (m_Xform.axis == XformAxis::All) scale = s.scale + glm::vec3(add);
                    else {
                        int i = static_cast<int>(m_Xform.axis) - 1;
                        scale = s.scale;
                        scale[i] = s.scale[i] + add;
                    }
                }

                transform::asVec3Mut(*s.e.get_mut<Position3>()) = pos;
                transform::asVec3Mut(*s.e.get_mut<RotationEuler>()) = euler;
                transform::asVec3Mut(*s.e.get_mut<Scale3>()) = scale;
                transform::markDirty(*s.e.get_mut<LocalMatrix>());
            }
            m_Context.needsRebuild = true;
            m_Context.needsRTRebuild = true;
        }

        void DrawEntityHover(flecs::entity entity, const Camera& camera, ui::Rect viewport) {
            glm::vec3 bmin, bmax;
            EntityLocalAabb(entity, bmin, bmax);
            glm::mat4 world = GetGlobalTransform(entity);
            glm::vec2 mn(1.0e8f), mx(-1.0e8f);
            bool any = false;
            for (int i = 0; i < 8; ++i) {
                glm::vec3 local(
                    (i & 1) ? bmax.x : bmin.x,
                    (i & 2) ? bmax.y : bmin.y,
                    (i & 4) ? bmax.z : bmin.z);
                glm::vec2 sp;
                if (!WorldToScreen(camera, glm::vec3(world * glm::vec4(local, 1.0f)), sp)) continue;
                mn = glm::min(mn, sp);
                mx = glm::max(mx, sp);
                any = true;
            }
            if (any) {
                ui::Rect box{mn.x, mn.y, mx.x - mn.x, mx.y - mn.y};
                box = box.Intersect(viewport);
                if (box.w > 2.0f && box.h > 2.0f) {
                    m_Widgets.Background(box, ui::Color::RGBA8(56, 132, 230, 45), 3.0f);
                    m_Widgets.Background({box.x, box.y, box.w, 2.0f}, ui::kTheme.accent);
                    m_Widgets.Background({box.x, box.y + box.h - 2.0f, box.w, 2.0f}, ui::kTheme.accent);
                    m_Widgets.Background({box.x, box.y, 2.0f, box.h}, ui::kTheme.accent);
                    m_Widgets.Background({box.x + box.w - 2.0f, box.y, 2.0f, box.h}, ui::kTheme.accent);
                }
            }
            m_Widgets.SetTooltip(EntityDisplayName(entity));
        }

        bool SelectionHasSelectedAncestor(flecs::entity entity) {
            flecs::entity e = entity;
            while (e.is_alive() && e.has<HierarchyComponent>()) {
                uint64_t pid = e.get<HierarchyComponent>()->parentID;
                if (pid == 0) break;
                flecs::entity parent = m_Context.FindEntityByID(pid);
                if (!parent.is_alive()) break;
                if (m_Context.IsSelected(parent)) return true;
                e = parent;
            }
            return false;
        }

        glm::vec3 GetMouseRay(BurnhopeWindow& window, Camera& camera) {
            glm::vec2 mouse = m_Input.MousePos();
            auto extent = window.getExtent();
            float w = camera.width > 0 ? (float)camera.width : (float)extent.width;
            float h = camera.height > 0 ? (float)camera.height : (float)extent.height;
            // Same clip space as Camera::GetProjectionMatrix (Y flipped for Vulkan).
            float ndcX = (2.0f * mouse.x) / w - 1.0f;
            float ndcY = (2.0f * mouse.y) / h - 1.0f;
            glm::mat4 invVP = glm::inverse(
                camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f) * camera.GetViewMatrix());
            auto unproject = [&](float ndcZ) {
                glm::vec4 p = invVP * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
                if (std::abs(p.w) > 1e-8f) p /= p.w;
                return glm::vec3(p);
            };
            glm::vec3 nearP = unproject(0.0f);
            glm::vec3 farP = unproject(1.0f);
            glm::vec3 dir = farP - nearP;
            float len = glm::length(dir);
            return len > 1e-8f ? dir / len : camera.Orientation;
        }

        bool TestRayOBB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::mat4 modelMatrix, float& tOutput,
                        glm::vec3 boxMin = glm::vec3(-1.0f), glm::vec3 boxMax = glm::vec3(1.0f)) {
            glm::mat4 invModel = glm::inverse(modelMatrix);
            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localDir = glm::vec3(invModel * glm::vec4(rayDir, 0.0f));
            float tMin = -1.0e8f, tMax = 1.0e8f;

            for (int i = 0; i < 3; i++) {
                if (std::abs(localDir[i]) < 1e-8f) {
                    if (localOrigin[i] < boxMin[i] || localOrigin[i] > boxMax[i]) return false;
                    continue;
                }
                float invD = 1.0f / localDir[i];
                float t1 = (boxMin[i] - localOrigin[i]) * invD;
                float t2 = (boxMax[i] - localOrigin[i]) * invD;
                if (invD < 0.0f) std::swap(t1, t2);
                tMin = t1 > tMin ? t1 : tMin;
                tMax = t2 < tMax ? t2 : tMax;
                if (tMin > tMax) return false;
            }
            tOutput = tMin >= 0.0f ? tMin : tMax;
            return tMax > 0.0f;
        }

        bool TestRayTriangle(glm::vec3 orig, glm::vec3 dir, glm::vec3 v0, glm::vec3 v1, glm::vec3 v2, float& tOut) {
            const float eps = 1e-7f;
            glm::vec3 e1 = v1 - v0;
            glm::vec3 e2 = v2 - v0;
            glm::vec3 p = glm::cross(dir, e2);
            float det = glm::dot(e1, p);
            if (std::abs(det) < eps) return false;
            float inv = 1.0f / det;
            glm::vec3 s = orig - v0;
            float u = glm::dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f) return false;
            glm::vec3 q = glm::cross(s, e1);
            float v = glm::dot(dir, q) * inv;
            if (v < 0.0f || u + v > 1.0f) return false;
            float t = glm::dot(e2, q) * inv;
            if (t <= eps) return false;
            tOut = t;
            return true;
        }

        static glm::vec3 UnpackPackedPos(const PackedVertexPos& p, glm::vec3 aabbMin, glm::vec3 aabbMax) {
            glm::vec3 n = glm::vec3(p.x, p.y, p.z) / 65535.0f;
            return n * (aabbMax - aabbMin) + aabbMin;
        }

        bool TestRayMesh(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::mat4 world, BurnhopeModel& model, float& tOut) {
            glm::mat4 inv = glm::inverse(world);
            glm::vec3 localO = glm::vec3(inv * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localD = glm::vec3(inv * glm::vec4(rayDir, 0.0f));

            const auto& pos = model.storedPositions;
            const auto& idx = model.storedIndices;
            if (pos.empty()) return false;

            auto fetch = [&](uint32_t i) {
                i = std::min(i, static_cast<uint32_t>(pos.size() - 1));
                return UnpackPackedPos(pos[i], model.globalAabbMin, model.globalAabbMax);
            };

            float best = 1.0e8f;
            bool hit = false;
            constexpr size_t kMaxTris = 250000;
            if (!idx.empty()) {
                const size_t triCount = idx.size() / 3;
                const size_t n = std::min(triCount, kMaxTris);
                for (size_t t = 0; t < n; ++t) {
                    float ht;
                    if (TestRayTriangle(localO, localD, fetch(idx[t * 3]), fetch(idx[t * 3 + 1]), fetch(idx[t * 3 + 2]), ht)
                        && ht < best) {
                        best = ht;
                        hit = true;
                    }
                }
            } else {
                const size_t triCount = pos.size() / 3;
                const size_t n = std::min(triCount, kMaxTris);
                for (size_t t = 0; t < n; ++t) {
                    float ht;
                    if (TestRayTriangle(localO, localD, fetch(uint32_t(t * 3)), fetch(uint32_t(t * 3 + 1)),
                                        fetch(uint32_t(t * 3 + 2)), ht) && ht < best) {
                        best = ht;
                        hit = true;
                    }
                }
            }
            if (!hit) return false;
            tOut = best;
            return true;
        }

        void EntityLocalAabb(flecs::entity entity, glm::vec3& boxMin, glm::vec3& boxMax) {
            boxMin = glm::vec3(-1.0f);
            boxMax = glm::vec3(1.0f);
            if (!entity.is_alive() || !entity.has<MeshComponent>()) return;
            const auto* mc = entity.get<MeshComponent>();
            if (!mc || !mc->model) return;
            const glm::vec3 mn = mc->model->globalAabbMin;
            const glm::vec3 mx = mc->model->globalAabbMax;
            if (glm::all(glm::lessThanEqual(mx, mn))) return;
            boxMin = mn;
            boxMax = mx;
        }

        glm::mat4 GetGlobalTransform(flecs::entity entity) {
            return transform::worldMatrix(entity);
        }

        flecs::entity CloneHierarchy(flecs::entity source, flecs::entity newParent) {
            if (!source.is_alive()) return flecs::entity();
            flecs::entity copy = m_Context.world->entity();
            copy.set<IDComponent>({});

            if (source.has<TagComponent>()) {
                TagComponent tag = *source.get<TagComponent>();
                if (!newParent.is_alive()) tag.name += " (Copy)";
                copy.set<TagComponent>(tag);
            }
            transform::copyBundle(source, copy);
            if (!newParent.is_alive() && transform::hasBundle(source))
                transform::writeLocal(copy, transform::decompose(transform::worldMatrix(source)));
            if (source.has<MeshComponent>()) copy.set<MeshComponent>(*source.get<MeshComponent>());
            if (source.has<LightComponent>()) copy.set<LightComponent>(*source.get<LightComponent>());
            if (source.has<ReflectionProbeComponent>()) copy.set<ReflectionProbeComponent>(*source.get<ReflectionProbeComponent>());
            if (source.has<DecalComponent>()) copy.set<DecalComponent>(*source.get<DecalComponent>());

            copy.set<HierarchyComponent>({});
            if (newParent.is_alive() && newParent.has<IDComponent>() && copy.has<IDComponent>()) {
                HierarchyComponent& hc = *copy.get_mut<HierarchyComponent>();
                hc.parentID = newParent.get<IDComponent>()->ID;
                if (!newParent.has<HierarchyComponent>()) newParent.set<HierarchyComponent>({});
                newParent.get_mut<HierarchyComponent>()->childrenIDs.push_back(copy.get<IDComponent>()->ID);
            } else if (transform::hasBundle(copy)) {
                glm::vec3 p = transform::asVec3(*copy.get<Position3>());
                p.x += 1.0f;
                transform::writeLocal(copy, p, transform::asVec3(*copy.get<RotationEuler>()),
                                      transform::asVec3(*copy.get<Scale3>()));
            }

            if (source.has<HierarchyComponent>()) {
                auto children = source.get<HierarchyComponent>()->childrenIDs;
                for (uint64_t cid : children) {
                    flecs::entity child = m_Context.FindEntityByID(cid);
                    if (child.is_alive()) CloneHierarchy(child, copy);
                }
            }
            return copy;
        }

        void Undo() {
            if (m_Xform.active) {
                CancelXform();
                return;
            }
            if (m_Context.undoStack.empty()) return;
            if (m_Device) vkDeviceWaitIdle(m_Device->device());
            m_Context.redoStack.push_back(m_Context.CaptureScene());
            SceneSnapshot snap = std::move(m_Context.undoStack.back());
            m_Context.undoStack.pop_back();
            m_Context.RestoreScene(snap);
        }

        void Redo() {
            if (m_Context.redoStack.empty()) return;
            if (m_Device) vkDeviceWaitIdle(m_Device->device());
            m_Context.undoStack.push_back(m_Context.CaptureScene());
            SceneSnapshot snap = std::move(m_Context.redoStack.back());
            m_Context.redoStack.pop_back();
            m_Context.RestoreScene(snap);
        }

        void RestoreMatHoverPreview() {
            if (!m_MatHoverBackup.active) return;
            if (m_MatHoverBackup.entity.is_alive() && m_MatHoverBackup.entity.has<MeshComponent>()) {
                auto& mc = *m_MatHoverBackup.entity.get_mut<MeshComponent>();
                mc.materials = m_MatHoverBackup.materials;
                mc.materialPaths = m_MatHoverBackup.paths;
                m_Context.needsRebuild = true;
            }
            m_MatHoverBackup = {};
        }

        void ApplyMatHoverPreview(flecs::entity hit, const std::string& path) {
            if (!hit.is_alive() || !hit.has<MeshComponent>()) return;
            auto& mc = *hit.get_mut<MeshComponent>();

            if (m_MatHoverBackup.active && m_MatHoverBackup.entity == hit && m_MatHoverPreviewPath == path) {
                m_Context.SelectOnly(hit);
                return;
            }

            if (m_MatHoverBackup.active && m_MatHoverBackup.entity != hit) RestoreMatHoverPreview();

            if (m_MatHoverPreviewPath != path || !m_MatHoverPreview) {
                vkDeviceWaitIdle(m_Device->device());
                m_MatHoverPreview = Material::loadFromJson(*m_Device, path);
                m_MatHoverPreviewPath = path;
            }
            if (!m_MatHoverPreview) return;

            if (!m_MatHoverBackup.active) {
                m_MatHoverBackup.entity = hit;
                m_MatHoverBackup.materials = mc.materials;
                m_MatHoverBackup.paths = mc.materialPaths;
                m_MatHoverBackup.active = true;
            }

            const size_t n = mc.model ? std::max<size_t>(1, mc.model->getSubMeshes().size())
                                      : std::max<size_t>(1, mc.materials.size());
            if (mc.materials.size() < n) mc.materials.resize(n, nullptr);
            if (mc.materialPaths.size() < n) mc.materialPaths.resize(n, "");
            for (size_t i = 0; i < n; ++i) {
                mc.materials[i] = m_MatHoverPreview;
                mc.materialPaths[i] = path;
            }
            m_Context.SelectOnly(hit);
            m_Context.needsRebuild = true;
        }

        void CommitMatHoverPreview(flecs::entity hit, const std::string& path) {
            RestoreMatHoverPreview();
            if (!hit.is_alive() || !hit.has<MeshComponent>()) return;
            m_Context.SaveState();
            m_Context.pendingMatLoadPath = path;
            m_Context.pendingMatEntity = hit;
            m_Context.pendingMatSlot = UINT32_MAX;
            m_Context.SelectOnly(hit);
            ApplyMatHoverPreview(hit, path);
            m_MatHoverBackup = {};
        }

        flecs::entity PickEntityUnderMouse(BurnhopeWindow& window, Camera& camera, ui::Rect viewport) {
            if (!viewport.Contains(m_Input.MousePos().x, m_Input.MousePos().y)) return flecs::entity();
            glm::vec3 rayOrigin = camera.Position;
            glm::vec3 rayDir = GetMouseRay(window, camera);
            float closestT = 1.0e8f;
            flecs::entity hit = flecs::entity();
            m_Context.world->each<Position3>([&](flecs::entity entity, Position3&) {
                if (!entity.has<MeshComponent>() || !transform::hasBundle(entity)) return;
                glm::vec3 bmin, bmax;
                EntityLocalAabb(entity, bmin, bmax);
                glm::mat4 world = GetGlobalTransform(entity);
                float t = 0.0f;
                if (!TestRayOBB(rayOrigin, rayDir, world, t, bmin, bmax) || t <= 0.0f || t >= closestT) return;

                const MeshComponent* mc = entity.get<MeshComponent>();
                if (mc && mc->model && !mc->model->storedPositions.empty()) {
                    float meshT = 0.0f;
                    if (!TestRayMesh(rayOrigin, rayDir, world, *mc->model, meshT) || meshT <= 0.0f) return;
                    t = meshT;
                    if (t >= closestT) return;
                }
                closestT = t;
                hit = entity;
            });
            return hit;
        }

        void TryDropAssetOnViewport(BurnhopeWindow& window, Camera& camera, ui::Rect viewport) {
            const auto* payload = m_Widgets.PeekDragDropPayload("CONTENT_BROWSER_ITEM");
            if (!payload) {
                RestoreMatHoverPreview();
                m_MatHoverPreview.reset();
                m_MatHoverPreviewPath.clear();
                return;
            }
            const std::string* pathStr = std::any_cast<std::string>(payload);
            if (!pathStr) return;

            std::filesystem::path path(*pathStr);
            std::string ext = path.extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const bool isMesh = ext == ".bhmesh" || ext == ".bhmodel" || ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb";
            const bool isMat = ext == ".bhmat" || ext == ".json";

            if (isMat) {
                if (!viewport.Contains(m_Input.MousePos().x, m_Input.MousePos().y)) {
                    RestoreMatHoverPreview();
                    return;
                }
                flecs::entity hit = PickEntityUnderMouse(window, camera, viewport);
                if (!hit.is_alive() || !hit.has<MeshComponent>()) {
                    RestoreMatHoverPreview();
                    return;
                }
                ApplyMatHoverPreview(hit, *pathStr);
                if (m_Input.MouseReleased(0)) CommitMatHoverPreview(hit, *pathStr);
                return;
            }

            if (!viewport.Contains(m_Input.MousePos().x, m_Input.MousePos().y)) return;
            if (!isMesh || !m_Input.MouseReleased(0)) return;

            m_Context.SaveState();
            flecs::entity e = m_Context.CreateBaseEntity(path.stem().string());
            glm::vec3 spawn = camera.Position + glm::normalize(camera.Orientation) * 4.0f;
            if (transform::hasBundle(e)) {
                transform::asVec3Mut(*e.get_mut<Position3>()) = spawn;
                transform::markDirty(*e.get_mut<LocalMatrix>());
            }
            e.set<MeshComponent>({});
            m_Context.pendingModelLoadPath = *pathStr;
            m_Context.pendingModelEntity = e;
            m_Context.SelectOnly(e);
        }

        UIContext m_Context;
        std::vector<std::unique_ptr<IUIWindow>> m_Windows;

        BurnhopeDevice* m_Device;

        project::ProjectFile m_Project;
        project::RecentFilesStore m_Recent;

        ui::UIInput m_Input;
        ui::UIRenderer m_Renderer;
        ui::UIText m_Text;
        ui::UIWidgets m_Widgets;
        ui::UIDockspace m_Dockspace;
        ui::SceneBrowserModal m_SceneBrowser;
        project::SceneController m_SceneController;
        std::unique_ptr<ui::MaterialPreview> m_MatPreview;

        bool m_WantCaptureMouse = false;
        XformOp m_GizmoOp = XformOp::Translate;
        bool m_GizmoLocal = false;
        bool m_PivotCenter = false;
        float m_SnapTranslate = 0.0f;
        float m_SnapRotate = 0.0f;
        float m_SnapScale = 0.0f;
        TransformSession m_Xform;
        ui::Rect m_LastViewport{};

        struct MatHoverBackup {
            flecs::entity entity;
            std::vector<std::shared_ptr<Material>> materials;
            std::vector<std::string> paths;
            bool active = false;
        };
        MatHoverBackup m_MatHoverBackup;
        std::shared_ptr<Material> m_MatHoverPreview;
        std::string m_MatHoverPreviewPath;
    };
}
