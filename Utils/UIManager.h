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

#include "UIContext.h"
#include "IUIWindow.h"
#include "InspectorWindow.h"
#include "OutlinerWindow.h"
#include "ContentBrowserWindow.h"
#include "PropertiesWindow.h"

namespace burnhope {
    class UIManager {
    private:
        UIContext m_Context;
        std::vector<std::unique_ptr<IUIWindow>> m_Windows;
        
        VkDescriptorPool m_ImguiPool;
        BurnhopeDevice* m_Device;
        bool m_ResetLayout = true; // true, чтобы при первом запуске окна встали на свои места
        bool m_WasUsingGizmo = false;

    public:
        UIContext& GetContext() { return m_Context; }
        UIManager(BurnhopeWindow& window, BurnhopeDevice& device, VkRenderPass renderPass, entt::registry* registry, const std::string& projectPath) {
            m_Device = &device;
            m_Context.registry = registry;
            m_Context.projectDirectory = projectPath;
            m_Context.currentDirectory = projectPath;

            InitImGui(window, device, renderPass);
            SetupTheme();

            // Регистрируем окна
            m_Windows.push_back(std::make_unique<OutlinerWindow>());
            m_Windows.push_back(std::make_unique<InspectorWindow>());
            m_Windows.push_back(std::make_unique<ContentBrowserWindow>());
            m_Windows.push_back(std::make_unique<PropertiesWindow>());
        }

        ~UIManager() {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            vkDestroyDescriptorPool(m_Device->device(), m_ImguiPool, nullptr);
        }

        void Draw(BurnhopeWindow& window, Camera& camera, VkCommandBuffer commandBuffer) {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();

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
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
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

            auto& hc = m_Context.registry->emplace<HierarchyComponent>(copy);
            if (newParent != entt::null && m_Context.registry->all_of<IDComponent>(newParent)) {
                hc.parentID = m_Context.registry->get<IDComponent>(newParent).ID;
                m_Context.registry->get<HierarchyComponent>(newParent).childrenIDs.push_back(m_Context.registry->get<IDComponent>(copy).ID);
            }
            return copy;
        }

        void Undo() {
            if (m_Context.undoStack.empty()) return;
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
                ImGui::DockBuilderFinish(dockspace_id);
            }
            ImGui::End();
        }

        void DrawMainMenuBar() {
            if (ImGui::BeginMainMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New Scene")) m_Context.registry->clear();
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
            style.WindowRounding = 6.0f;
            style.ChildRounding = 4.0f;
            style.FrameRounding = 4.0f;
            style.PopupRounding = 6.0f;
            style.TabRounding = 6.0f;
            style.WindowBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.ItemSpacing = ImVec2(8, 6);
            colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
            colors[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.10f, 0.13f, 0.98f);
            colors[ImGuiCol_Border] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.35f, 0.22f, 0.50f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.48f, 0.30f, 0.68f, 1.00f);
            colors[ImGuiCol_Header] = ImVec4(0.20f, 0.16f, 0.26f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.25f, 0.55f, 1.00f);
            colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.12f, 0.17f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.20f, 0.38f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.24f, 0.18f, 0.32f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.68f, 0.45f, 0.95f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.55f, 0.35f, 0.85f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.50f, 1.00f, 1.00f);
            colors[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.10f, 0.13f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.14f, 0.20f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.09f, 0.08f, 0.11f, 1.00f);
        }
    };
}