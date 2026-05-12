#pragma once
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <GLFW/glfw3.h>
#include "../Render/Camera.hpp"
#include "imgui_impl_vulkan.h"
#include <imgui_impl_glfw.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <iostream>

#include "UIContext.h"
#include "IUIWindow.h"
#include "InspectorWindow.h"
#include "OutlinerWindow.h"
#include "ContentBrowserWindow.h"
#include "PropertiesWindow.h"
#include "MaterialEditorWindow.h"

namespace burnhope {
    class UIManager {
    private:
        UIContext m_Context;
        std::vector<std::unique_ptr<IUIWindow>> m_Windows;
        
        VkDescriptorPool m_ImguiPool;
        BurnhopeDevice* m_Device;
        bool m_ResetLayout = true; // true, чтобы при первом запуске окна встали на свои места
        bool m_WasUsingGizmo = false;
        bool m_OpenLoadScenePopup = false;

    public:
        UIContext& GetContext() { return m_Context; }
        UIManager(BurnhopeWindow& window, BurnhopeDevice& device, VkRenderPass renderPass, entt::registry* registry, const std::string& projectPath) {
            m_Device = &device;
            m_Context.registry = registry;
            m_Context.device = &device;
            m_Context.projectDirectory = projectPath;
            m_Context.currentDirectory = projectPath;

            InitImGui(window, device, renderPass);
            SetupTheme();

            // ЗАГРУЗКА НАСТРОЕК
            m_Context.LoadRenderSettings(projectPath + "/rendersettings.json");

            // Регистрируем окна
            m_Windows.push_back(std::make_unique<OutlinerWindow>());
            m_Windows.push_back(std::make_unique<InspectorWindow>());
            m_Windows.push_back(std::make_unique<ContentBrowserWindow>());
            m_Windows.push_back(std::make_unique<PropertiesWindow>());
            m_Windows.push_back(std::make_unique<MaterialEditorWindow>());
        }

        ~UIManager() {
            // Ждем завершения работы GPU перед выгрузкой ресурсов
            vkDeviceWaitIdle(m_Device->device());
            // Очищаем окна (убивает превью текстуры и открытые материалы)
            m_Windows.clear();
            // Очищаем историю сцен (убивает закешированные модели и буферы)
            m_Context.undoStack.clear();
            m_Context.redoStack.clear();

            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            vkDestroyDescriptorPool(m_Device->device(), m_ImguiPool, nullptr);
        }

        void UpdateUI(BurnhopeWindow& window, Camera& camera, VkCommandBuffer commandBuffer) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
            m_Context.currentCommandBuffer = commandBuffer;

            HandleGlobalHotkeys();
            DrawMainMenuBar();
            DrawDockSpace();

            // Логика выделения объектов мышкой и отрисовка Гизмо
            HandleGizmosAndSelection(window, camera);

            // Отрисовка всех окон
            for (auto& win : m_Windows) {
                win->Draw(m_Context);
            }

            // Рендер в Vulkan
            ImGui::Render();
        }

        void RenderUI(VkCommandBuffer commandBuffer) {
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }

        void ProcessPendingActions() {
            if (m_Context.pendingNewScene) {
                vkDeviceWaitIdle(m_Device->device());
                m_Context.registry->clear();
                m_Context.undoStack.clear();
                m_Context.redoStack.clear();
                m_Context.safeDeleteQueue.clear();
                m_Context.pendingDeletions.clear();
                m_Context.selectedEntity = entt::null;
                m_Context.needsRebuild = true;
                m_Context.pendingNewScene = false;
                m_Context.currentScenePath = "";
            }
            if (!m_Context.pendingSceneLoadPath.empty()) {
                LoadScene(m_Context.pendingSceneLoadPath);
                m_Context.pendingSceneLoadPath = "";
            }
        }

    private:
        void HandleGlobalHotkeys() {
            ImGuiIO& io = ImGui::GetIO();
            if (!ImGui::IsAnyItemActive()) {
                // Undo / Redo
                if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
                if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Redo();
                
                // Duplicate
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && m_Context.selectedEntity != entt::null) {
                    m_Context.SaveState();
                    entt::entity parent = entt::null;
                    entt::entity newEnt = CloneHierarchy(m_Context.selectedEntity, parent);
                    m_Context.selectedEntity = newEnt;
                }
                
                // Save / Load
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                    std::string spath = m_Context.currentScenePath.empty() ? (m_Context.projectDirectory / "scene.burnscene").string() : m_Context.currentScenePath;
                    SaveScene(spath);
                }
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O)) m_OpenLoadScenePopup = true;
            }
        }

        void HandleGizmosAndSelection(BurnhopeWindow& window, Camera& camera) {
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
            auto extent = window.getExtent();
            ImGuizmo::SetRect(0, 0, (float)extent.width, (float)extent.height);

            static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
            static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;

            // Хоткеи для переключения режимов Гизмо
            if (!ImGui::IsAnyItemActive()) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmoOperation = ImGuizmo::TRANSLATE;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;
            }

            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);
            glm::mat4 gizmoProj = proj;
            gizmoProj[1][1] *= -1.0f; // Фикс для Vulkan

            // Выбор объекта мышкой (Raycasting)
            if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver()) {
                glm::vec3 rayOrigin = camera.Position;
                glm::vec3 rayDir = GetMouseRay(window, camera);
                float closestT = 1000.0f;
                entt::entity hitIndex = entt::null;

                auto pickView = m_Context.registry->view<TransformComponent>();
                pickView.each([&](entt::entity entity, TransformComponent& tComp) {
                    float t;
                    if (TestRayOBB(rayOrigin, rayDir, tComp.transform.matrix, t)) {
                        if (t < closestT) { closestT = t; hitIndex = entity; }
                    }
                });

                if (hitIndex != entt::null) {
                    m_Context.selectedEntity = hitIndex;
                }
            }

            // Отрисовка Гизмо и манипуляция
            if (m_Context.selectedEntity != entt::null && m_Context.registry->valid(m_Context.selectedEntity) && m_Context.registry->all_of<TransformComponent>(m_Context.selectedEntity)) {
                auto& tComp = m_Context.registry->get<TransformComponent>(m_Context.selectedEntity);
                tComp.transform.updateMatrixIfNeeded();

                // 1. Вычисляем мировую матрицу РОДИТЕЛЯ
                glm::mat4 parentWorldMatrix = glm::mat4(1.0f);
                if (m_Context.registry->all_of<HierarchyComponent>(m_Context.selectedEntity)) {
                    uint64_t pid = m_Context.registry->get<HierarchyComponent>(m_Context.selectedEntity).parentID;
                    entt::entity pEnt = m_Context.FindEntityByID(pid);
                    if (pEnt != entt::null) parentWorldMatrix = GetGlobalTransform(pEnt);
                }

                // 2. Итоговая мировая матрица этого объекта
                glm::mat4 worldMatrix = parentWorldMatrix * tComp.transform.matrix;

                if (ImGuizmo::IsUsing() && !m_WasUsingGizmo) m_Context.SaveState();
                m_WasUsingGizmo = ImGuizmo::IsUsing();

                // 3. Отдаем Гизмо МИРОВУЮ матрицу для отрисовки
                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(worldMatrix));

                // 4. Если мы потянули за стрелочку
                if (ImGuizmo::IsUsing()) {
                    // Вычитаем из новой мировой матрицы влияние родителя, чтобы получить новую ЛОКАЛЬНУЮ
                    glm::mat4 newLocalMatrix = glm::inverse(parentWorldMatrix) * worldMatrix;

                    ImGuizmo::DecomposeMatrixToComponents(
                        glm::value_ptr(newLocalMatrix),
                        glm::value_ptr(tComp.transform.position),
                        glm::value_ptr(tComp.transform.rotation),
                        glm::value_ptr(tComp.transform.scale)
                    );
                    tComp.transform.updatematrix = true;
                }
            }
        }

        // --- УТИЛИТЫ ДЛЯ RAYCAST И МАТЕМАТИКИ ---

        glm::vec3 GetMouseRay(BurnhopeWindow& window, Camera& camera) {
            double mouseX, mouseY;
            glfwGetCursorPos(window.getGLFWwindow(), &mouseX, &mouseY);
            auto extent = window.getExtent();
            float x = (2.0f * (float)mouseX) / extent.width - 1.0f;
            float y = 1.0f - (2.0f * (float)mouseY) / extent.height;
            glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix(45.0f, 0.1f, 100.0f));
            glm::mat4 invView = glm::inverse(glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up));
            glm::vec4 rayClip = glm::vec4(x, y, -1.0f, 1.0f);
            glm::vec4 rayEye = invProj * rayClip;
            rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
            return glm::normalize(glm::vec3(invView * rayEye));
        }

        bool TestRayOBB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::mat4 modelMatrix, float& tOutput) {
            glm::mat4 invModel = glm::inverse(modelMatrix);
            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));
            float tMin = -100000.0f, tMax = 100000.0f;
            glm::vec3 boxMin = glm::vec3(-1.0f), boxMax = glm::vec3(1.0f);
            
            for (int i = 0; i < 3; i++) {
                float invD = 1.0f / localDir[i];
                float t1 = (boxMin[i] - localOrigin[i]) * invD;
                float t2 = (boxMax[i] - localOrigin[i]) * invD;
                if (invD < 0.0f) std::swap(t1, t2);
                tMin = t1 > tMin ? t1 : tMin;
                tMax = t2 < tMax ? t2 : tMax;
                if (tMin > tMax) return false;
            }
            tOutput = tMin;
            return tMax > 0;
        }

        glm::mat4 GetGlobalTransform(entt::entity entity) {
            if (!m_Context.registry->valid(entity) || !m_Context.registry->all_of<TransformComponent>(entity)) return glm::mat4(1.0f);
            auto& tComp = m_Context.registry->get<TransformComponent>(entity);
            tComp.transform.updateMatrixIfNeeded();
            glm::mat4 globalMat = tComp.transform.matrix;

            if (m_Context.registry->all_of<HierarchyComponent>(entity)) {
                uint64_t parentID = m_Context.registry->get<HierarchyComponent>(entity).parentID;
                entt::entity parentEnt = m_Context.FindEntityByID(parentID);
                if (parentEnt != entt::null) globalMat = GetGlobalTransform(parentEnt) * globalMat;
            }
            return globalMat;
        }

        entt::entity CloneHierarchy(entt::entity source, entt::entity newParent) {
            entt::entity copy = m_Context.registry->create();
            m_Context.registry->emplace<IDComponent>(copy); // Новый уникальный ID

            if (m_Context.registry->all_of<TagComponent>(source)) {
                auto tag = m_Context.registry->get<TagComponent>(source);
                tag.name += " (Copy)";
                m_Context.registry->emplace<TagComponent>(copy, tag);
            }
            if (m_Context.registry->all_of<TransformComponent>(source)) m_Context.registry->emplace<TransformComponent>(copy, m_Context.registry->get<TransformComponent>(source));
            if (m_Context.registry->all_of<MeshComponent>(source)) m_Context.registry->emplace<MeshComponent>(copy, m_Context.registry->get<MeshComponent>(source));
            if (m_Context.registry->all_of<LightComponent>(source)) m_Context.registry->emplace<LightComponent>(copy, m_Context.registry->get<LightComponent>(source));
            if (m_Context.registry->all_of<ReflectionProbeComponent>(source)) m_Context.registry->emplace<ReflectionProbeComponent>(copy, m_Context.registry->get<ReflectionProbeComponent>(source));

            auto& hc = m_Context.registry->emplace<HierarchyComponent>(copy);
            if (newParent != entt::null && m_Context.registry->all_of<IDComponent>(newParent)) {
                hc.parentID = m_Context.registry->get<IDComponent>(newParent).ID;
                m_Context.registry->get<HierarchyComponent>(newParent).childrenIDs.push_back(m_Context.registry->get<IDComponent>(copy).ID);
            }
            return copy;
        }

        void Undo() {
            if (m_Context.undoStack.empty()) return;
            vkDeviceWaitIdle(m_Device->device());
            auto snapReg = std::make_shared<entt::registry>();
            m_Context.CopyRegistry(*m_Context.registry, *snapReg);
            m_Context.redoStack.push_back({snapReg, m_Context.selectedEntity});
            
            SceneSnapshot snap = m_Context.undoStack.back();
            m_Context.undoStack.pop_back();
            m_Context.CopyRegistry(*snap.regCopy, *m_Context.registry);
            m_Context.selectedEntity = snap.selectedEntity;
        }

        void Redo() {
            if (m_Context.redoStack.empty()) return;
            vkDeviceWaitIdle(m_Device->device());
            auto snapReg = std::make_shared<entt::registry>();
            m_Context.CopyRegistry(*m_Context.registry, *snapReg);
            m_Context.undoStack.push_back({snapReg, m_Context.selectedEntity});
            
            SceneSnapshot snap = m_Context.redoStack.back();
            m_Context.redoStack.pop_back();
            m_Context.CopyRegistry(*snap.regCopy, *m_Context.registry);
            m_Context.selectedEntity = snap.selectedEntity;
        }

        // --- ИНИЦИАЛИЗАЦИЯ И ОТРИСОВКА БАЗОВОГО UI ---

        void DrawDockSpace() {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("MainDockSpace", nullptr, window_flags);
            ImGui::PopStyleVar(3);

            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

            if (m_ResetLayout) {
                m_ResetLayout = false;
                ImGui::DockBuilderRemoveNode(dockspace_id);
                ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
                ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
                
                auto dock_main = dockspace_id;
                auto dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.15f, nullptr, &dock_main);
                auto dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, nullptr, &dock_main);
                auto dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);
                
                ImGui::DockBuilderDockWindow("Scene Outliner", dock_left);
                ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
                ImGui::DockBuilderDockWindow("Scene Inspector", dock_right);
                ImGui::DockBuilderDockWindow("Properties", dock_right);
                ImGui::DockBuilderDockWindow("Material Editor", dock_right);
                ImGui::DockBuilderFinish(dockspace_id);
            }
            ImGui::End();
        }

        void DrawMainMenuBar() {
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New Scene")) {
                        m_Context.pendingNewScene = true;
                    }
                    if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                        std::string spath = m_Context.currentScenePath.empty() ? (m_Context.projectDirectory / "scene.burnscene").string() : m_Context.currentScenePath;
                        SaveScene(spath);
                    }
                    if (ImGui::MenuItem("Load Scene", "Ctrl+O")) m_OpenLoadScenePopup = true;
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit")) {
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !m_Context.undoStack.empty())) Undo();
                    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !m_Context.redoStack.empty())) Redo();
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window")) {
                    for (auto& win : m_Windows) {
                        ImGui::MenuItem(win->m_Name.c_str(), NULL, &win->m_IsOpen);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Reset Layout")) m_ResetLayout = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            if (m_OpenLoadScenePopup) {
                ImGui::OpenPopup("Load Scene");
                m_OpenLoadScenePopup = false;
            }
            if (ImGui::BeginPopupModal("Load Scene", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                auto scenes = m_Context.GetProjectAssets({".burnscene", ".json"});
                if (scenes.empty()) ImGui::TextDisabled("No scenes found (.burnscene, .json)");
                for (const auto& s : scenes) {
                    if (ImGui::Selectable(std::filesystem::path(s).filename().string().c_str())) {
                        m_Context.pendingSceneLoadPath = s;
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::Separator();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
        }

        void InitImGui(BurnhopeWindow& window, BurnhopeDevice& device, VkRenderPass renderPass) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuizmo::Enable(true);

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

            VkDescriptorPoolSize pool_sizes[] = { 
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1000}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
            };
            
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000;
            pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            vkCreateDescriptorPool(device.device(), &pool_info, nullptr, &m_ImguiPool);

            ImGui_ImplGlfw_InitForVulkan(window.getGLFWwindow(), true);
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = device.getInstance();
            init_info.PhysicalDevice = device.getPhysicalDevice();
            init_info.Device = device.device();
            init_info.Queue = device.graphicsQueue();
            init_info.DescriptorPool = m_ImguiPool;
            init_info.MinImageCount = 2;
            init_info.ImageCount = 3;
            init_info.PipelineInfoMain.RenderPass = renderPass;
            ImGui_ImplVulkan_Init(&init_info);
        }

        void SetupTheme() {
            ImGuiStyle &style = ImGui::GetStyle();
            ImVec4 *colors = style.Colors;

            // --- Unreal Engine 5 / Modern Flat UI Styling ---
            style.WindowPadding     = ImVec2(8.0f, 8.0f);
            style.FramePadding      = ImVec2(6.0f, 4.0f);
            style.ItemSpacing       = ImVec2(8.0f, 4.0f);
            style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
            style.IndentSpacing     = 12.0f;
            style.ScrollbarSize     = 12.0f;
            style.GrabMinSize       = 10.0f;
            
            // Minimal borders for definition
            style.WindowBorderSize  = 1.0f;
            style.ChildBorderSize   = 1.0f;
            style.PopupBorderSize   = 1.0f;
            style.FrameBorderSize   = 1.0f;
            style.TabBorderSize     = 1.0f;
            
            // Flat design with very slight rounding
            style.WindowRounding    = 3.0f;
            style.ChildRounding     = 2.0f;
            style.FrameRounding     = 2.0f;
            style.PopupRounding     = 3.0f;
            style.ScrollbarRounding = 2.0f;
            style.GrabRounding      = 2.0f;
            style.TabRounding       = 2.0f;

            // --- Deep Dark / Blue Accent Palette ---
            ImVec4 bg_base          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
            ImVec4 bg_panel         = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
            ImVec4 bg_popup         = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            
            ImVec4 accent_base      = ImVec4(0.00f, 0.45f, 0.85f, 1.00f);
            ImVec4 accent_hover     = ImVec4(0.10f, 0.55f, 0.95f, 1.00f);
            ImVec4 accent_active    = ImVec4(0.20f, 0.65f, 1.00f, 1.00f);
            
            ImVec4 frame_bg         = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
            ImVec4 frame_hover      = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
            
            ImVec4 border           = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            
            // Text
            ImVec4 text_main        = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
            ImVec4 text_muted       = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

            colors[ImGuiCol_Text]                  = text_main;
            colors[ImGuiCol_TextDisabled]          = text_muted;
            colors[ImGuiCol_WindowBg]              = bg_base;
            colors[ImGuiCol_ChildBg]               = bg_panel;
            colors[ImGuiCol_PopupBg]               = bg_popup;
            colors[ImGuiCol_Border]                = border;
            colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg]               = frame_bg;
            colors[ImGuiCol_FrameBgHovered]        = frame_hover;
            colors[ImGuiCol_FrameBgActive]         = accent_active;
            colors[ImGuiCol_TitleBg]               = bg_base;
            colors[ImGuiCol_TitleBgActive]         = bg_base;
            colors[ImGuiCol_TitleBgCollapsed]      = bg_base;
            colors[ImGuiCol_MenuBarBg]             = bg_panel;
            colors[ImGuiCol_ScrollbarBg]           = bg_base;
            colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]   = accent_active;
            colors[ImGuiCol_CheckMark]             = accent_active;
            colors[ImGuiCol_SliderGrab]            = accent_base;
            colors[ImGuiCol_SliderGrabActive]      = accent_active;
            colors[ImGuiCol_Button]                = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
            colors[ImGuiCol_ButtonHovered]         = frame_hover;
            colors[ImGuiCol_ButtonActive]          = accent_active;
            colors[ImGuiCol_Header]                = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
            colors[ImGuiCol_HeaderHovered]         = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
            colors[ImGuiCol_HeaderActive]          = accent_active;
            colors[ImGuiCol_Separator]             = colors[ImGuiCol_Border];
            colors[ImGuiCol_SeparatorHovered]      = accent_hover;
            colors[ImGuiCol_SeparatorActive]       = accent_active;
            colors[ImGuiCol_ResizeGrip]            = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
            colors[ImGuiCol_ResizeGripHovered]     = accent_hover;
            colors[ImGuiCol_ResizeGripActive]      = accent_active;
            colors[ImGuiCol_Tab]                   = bg_base;
            colors[ImGuiCol_TabHovered]            = frame_hover;
            colors[ImGuiCol_TabActive]             = bg_panel;
            colors[ImGuiCol_TabUnfocused]          = bg_base;
            colors[ImGuiCol_TabUnfocusedActive]    = bg_panel;
            colors[ImGuiCol_PlotLines]             = accent_base;
            colors[ImGuiCol_PlotLinesHovered]      = accent_hover;
            colors[ImGuiCol_PlotHistogram]         = accent_base;
            colors[ImGuiCol_PlotHistogramHovered]  = accent_hover;
            colors[ImGuiCol_TableHeaderBg]         = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]     = border;
            colors[ImGuiCol_TableBorderLight]      = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            colors[ImGuiCol_TableRowBg]            = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]         = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]        = ImVec4(0.00f, 0.44f, 0.87f, 0.50f);
            colors[ImGuiCol_DragDropTarget]        = accent_active;
            colors[ImGuiCol_NavHighlight]          = accent_active;
        }

    private:
        void SaveScene(const std::string& filepath) {
            nlohmann::json sceneJson;
            sceneJson["Entities"] = nlohmann::json::array();

            auto view = m_Context.registry->view<IDComponent>();
            for (auto entity : view) {
                nlohmann::json entityJson;
                
                if (m_Context.registry->all_of<IDComponent>(entity)) {
                    entityJson["IDComponent"]["ID"] = m_Context.registry->get<IDComponent>(entity).ID;
                }
                
                if (m_Context.registry->all_of<TagComponent>(entity)) {
                    entityJson["TagComponent"]["Name"] = m_Context.registry->get<TagComponent>(entity).name;
                }
                
                if (m_Context.registry->all_of<TransformComponent>(entity)) {
                    auto& tc = m_Context.registry->get<TransformComponent>(entity);
                    entityJson["TransformComponent"]["Position"] = {tc.transform.position.x, tc.transform.position.y, tc.transform.position.z};
                    entityJson["TransformComponent"]["Rotation"] = {tc.transform.rotation.x, tc.transform.rotation.y, tc.transform.rotation.z};
                    entityJson["TransformComponent"]["Scale"] = {tc.transform.scale.x, tc.transform.scale.y, tc.transform.scale.z};
                }

                if (m_Context.registry->all_of<HierarchyComponent>(entity)) {
                    auto& hc = m_Context.registry->get<HierarchyComponent>(entity);
                    entityJson["HierarchyComponent"]["ParentID"] = hc.parentID;
                    entityJson["HierarchyComponent"]["ChildrenIDs"] = hc.childrenIDs;
                }

                if (m_Context.registry->all_of<MeshComponent>(entity)) {
                    auto& mc = m_Context.registry->get<MeshComponent>(entity);
                    entityJson["MeshComponent"]["ModelPath"] = mc.modelPath;
                    entityJson["MeshComponent"]["MaterialPaths"] = mc.materialPaths;
                    entityJson["MeshComponent"]["IsStatic"] = mc.isStatic;
                    entityJson["MeshComponent"]["IsVisible"] = mc.isVisible;
                    entityJson["MeshComponent"]["CastShadow"] = mc.castShadow;
                }

                if (m_Context.registry->all_of<LightComponent>(entity)) {
                    auto& lc = m_Context.registry->get<LightComponent>(entity);
                    entityJson["LightComponent"]["NeedsShadowUpdate"] = lc.needsShadowUpdate;
                    entityJson["LightComponent"]["Enable"] = lc.light.enable;
                    entityJson["LightComponent"]["Type"] = static_cast<int>(lc.light.type);
                    entityJson["LightComponent"]["Color"] = {lc.light.color.x, lc.light.color.y, lc.light.color.z};
                    entityJson["LightComponent"]["Intensity"] = lc.light.intensity;
                    entityJson["LightComponent"]["Radius"] = lc.light.radius;
                    entityJson["LightComponent"]["CastShadows"] = lc.light.castShadows;
                    entityJson["LightComponent"]["Mobility"] = static_cast<int>(lc.light.mobility);
                }

                if (m_Context.registry->all_of<ReflectionProbeComponent>(entity)) {
                    auto& rpc = m_Context.registry->get<ReflectionProbeComponent>(entity);
                    entityJson["ReflectionProbeComponent"]["Radius"] = rpc.radius;
                    entityJson["ReflectionProbeComponent"]["Resolution"] = rpc.resolution;
                }

                if (m_Context.registry->all_of<DecalComponent>(entity)) {
                    auto& dc = m_Context.registry->get<DecalComponent>(entity);
                    entityJson["DecalComponent"]["AlbedoPath"] = dc.albedoPath;
                    entityJson["DecalComponent"]["NormalPath"] = dc.normalPath;
                    entityJson["DecalComponent"]["Opacity"] = dc.opacity;
                }

                sceneJson["Entities"].push_back(entityJson);
            }

            std::ofstream file(filepath);
            file << sceneJson.dump(4);
        }

        void LoadScene(const std::string& filepath) {
            std::ifstream file(filepath);
            if (!file.is_open()) return;

            nlohmann::json sceneJson;
            file >> sceneJson;

            // Ждем, пока видеокарта закончит кадр, чтобы безопасно выгрузить старые модели
            vkDeviceWaitIdle(m_Device->device());

            m_Context.registry->clear();
            m_Context.undoStack.clear();
            m_Context.redoStack.clear();
            m_Context.safeDeleteQueue.clear();
            m_Context.pendingDeletions.clear();
            m_Context.selectedEntity = entt::null;
            m_Context.needsRebuild = true;
            m_Context.currentScenePath = filepath;

            for (const auto& entityJson : sceneJson["Entities"]) {
                entt::entity entity = m_Context.registry->create();

                if (entityJson.contains("IDComponent")) {
                    m_Context.registry->emplace<IDComponent>(entity, entityJson["IDComponent"]["ID"].get<uint64_t>());
                } else {
                    m_Context.registry->emplace<IDComponent>(entity);
                }

                if (entityJson.contains("TagComponent")) {
                    auto& tc = m_Context.registry->emplace<TagComponent>(entity);
                    tc.name = entityJson["TagComponent"]["Name"];
                }

                if (entityJson.contains("TransformComponent")) {
                    auto& tc = m_Context.registry->emplace<TransformComponent>(entity);
                    tc.transform.position = glm::vec3(entityJson["TransformComponent"]["Position"][0], entityJson["TransformComponent"]["Position"][1], entityJson["TransformComponent"]["Position"][2]);
                    tc.transform.rotation = glm::vec3(entityJson["TransformComponent"]["Rotation"][0], entityJson["TransformComponent"]["Rotation"][1], entityJson["TransformComponent"]["Rotation"][2]);
                    tc.transform.scale = glm::vec3(entityJson["TransformComponent"]["Scale"][0], entityJson["TransformComponent"]["Scale"][1], entityJson["TransformComponent"]["Scale"][2]);
                    tc.transform.updatematrix = true;
                }

                if (entityJson.contains("HierarchyComponent")) {
                    auto& hc = m_Context.registry->emplace<HierarchyComponent>(entity);
                    hc.parentID = entityJson["HierarchyComponent"]["ParentID"];
                    for (const auto& childId : entityJson["HierarchyComponent"]["ChildrenIDs"]) {
                        hc.childrenIDs.push_back(childId);
                    }
                }

                if (entityJson.contains("MeshComponent")) {
                    auto& mc = m_Context.registry->emplace<MeshComponent>(entity);
                    mc.modelPath = entityJson["MeshComponent"].value("ModelPath", "");
                    if (entityJson["MeshComponent"].contains("MaterialPaths")) {
                        for (const auto& path : entityJson["MeshComponent"]["MaterialPaths"]) {
                        std::string pathStr = path.get<std::string>();
                        mc.materialPaths.push_back(pathStr);
                        if (m_Device && !pathStr.empty()) {
                            mc.materials.push_back(Material::loadFromJson(*m_Device, pathStr));
                        } else {
                            mc.materials.push_back(nullptr);
                        }
                        }
                    }
                    mc.isStatic = entityJson["MeshComponent"].value("IsStatic", false);
                    mc.isVisible = entityJson["MeshComponent"].value("IsVisible", true);
                    mc.castShadow = entityJson["MeshComponent"].value("CastShadow", true);
                    
                    // Если путь указан, сразу загружаем 3D-модель (инициализируем буферы)
                    if (!mc.modelPath.empty() && m_Device) {
                        try {
                            mc.model = BurnhopeModel::createModelFromFile(*m_Device, mc.modelPath);
                        // Защита от краша: выравниваем размер массива материалов под количество сабмешей
                        if (mc.materials.size() < mc.model->getSubMeshes().size()) {
                            mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                        }
                        if (mc.materialPaths.size() < mc.model->getSubMeshes().size()) {
                            mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                        }
                        } catch (const std::exception& e) {
                            std::cerr << "[ERROR] Failed to load model: " << e.what() << "\n";
                        }
                    }
                }

                if (entityJson.contains("LightComponent")) {
                    auto& lc = m_Context.registry->emplace<LightComponent>(entity);
                    lc.needsShadowUpdate = true; // Принудительное обновление теней при загрузке
                    
                    if (entityJson["LightComponent"].contains("Enable")) lc.light.enable = entityJson["LightComponent"]["Enable"].get<bool>();
                    if (entityJson["LightComponent"].contains("Type")) lc.light.type = static_cast<LightType>(entityJson["LightComponent"]["Type"].get<int>());
                    if (entityJson["LightComponent"].contains("Color")) {
                        lc.light.color = glm::vec3(
                            entityJson["LightComponent"]["Color"][0].get<float>(), 
                            entityJson["LightComponent"]["Color"][1].get<float>(), 
                            entityJson["LightComponent"]["Color"][2].get<float>());
                    }
                    if (entityJson["LightComponent"].contains("Intensity")) lc.light.intensity = entityJson["LightComponent"]["Intensity"].get<float>();
                    if (entityJson["LightComponent"].contains("Radius")) lc.light.radius = entityJson["LightComponent"]["Radius"].get<float>();
                    if (entityJson["LightComponent"].contains("CastShadows")) lc.light.castShadows = entityJson["LightComponent"]["CastShadows"].get<bool>();
                    if (entityJson["LightComponent"].contains("Mobility")) lc.light.mobility = static_cast<LightMobility>(entityJson["LightComponent"]["Mobility"].get<int>());
                }

                if (entityJson.contains("ReflectionProbeComponent")) {
                    auto& rpc = m_Context.registry->emplace<ReflectionProbeComponent>(entity);
                    rpc.radius = entityJson["ReflectionProbeComponent"].value("Radius", 10.0f);
                    rpc.resolution = entityJson["ReflectionProbeComponent"].value("Resolution", 256);
                    rpc.updateNeeded = true;
                }
                
                if (entityJson.contains("DecalComponent")) {
                    auto& dc = m_Context.registry->emplace<DecalComponent>(entity);
                    dc.albedoPath = entityJson["DecalComponent"].value("AlbedoPath", "");
                    dc.normalPath = entityJson["DecalComponent"].value("NormalPath", "");
                    dc.opacity = entityJson["DecalComponent"].value("Opacity", 1.0f);
                    if (!dc.albedoPath.empty() && m_Device) dc.albedoTex = BurnhopeTexture::createTextureFromFile(*m_Device, dc.albedoPath);
                    if (!dc.normalPath.empty() && m_Device) dc.normalTex = BurnhopeTexture::createDataTextureFromFile(*m_Device, dc.normalPath);
                }
            }
        }
    };
}