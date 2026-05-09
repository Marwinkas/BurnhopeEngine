#ifndef UI_CLASS_H
#define UI_CLASS_H
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.h>
#include "imgui_impl_vulkan.h"
#include "Device.hpp"
#include <vector>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream>
#include "imgui.h"
#include "imgui_internal.h"
#include "ImGuizmo.h"
#include "Components.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifndef _WIN32
#define MAX_PATH 4096
template <size_t size>
inline void strcpy_s(char (&dest)[size], const char *src)
{
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}
inline void strcpy_s(char *dest, size_t size, const char *src)
{
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}
#define GetModuleFileNameA(unused, buf, size)
#endif
#include <unordered_map>
#include <fstream>
#include <nlohmann/json.hpp>
#include "stb_image.h"
#include <algorithm>
#include <memory>
#include "../Render/ModelImporter.h"
#include "FrameInfo.hpp"
#include <imgui_impl_glfw.h>
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace burnhope
{
    class UI
    {
    public:
        entt::entity selectedEntity = entt::null;
        glm::mat4 model = glm::mat4(1.0f);
        fs::path projectDirectory;
        fs::path ExeDirectory;
        fs::path currentDirectory;
        std::vector<fs::path> dirHistory;
        int dirHistoryIndex = -1;
        std::vector<std::string> selectedAssets;
        int lastClickedIndex = -1;
        char searchBuffer[256] = "";
        std::vector<std::string> clipboardPaths;
        bool isCut = false;
        std::string renamingPath = "";
        char inlineRenameBuf[256] = "";
        bool focusRename = false;
        char editAlbedo[256] = "";
        char editNormal[256] = "";
        char editMetallic[256] = "";
        char editRoughness[256] = "";
        char editHeight[256] = "";
        char editAO[256] = "";
        char editEmissive[256] = "";
        float editEmissiveIntensity = 1.0f;
        std::unordered_map<std::string, GLuint> imageThumbnails;
        bool showOutliner = true;
        bool showInspector = true;
        bool showProperties = true;
        bool showContentBrowser = true;
        bool resetLayout = false;
        bool showAboutModal = false;
        BurnhopeDevice *m_device;
        struct SceneSnapshot
        {
            std::shared_ptr<entt::registry> regCopy;
            entt::entity selectedEntity;
        };
        std::vector<SceneSnapshot> undoStack;
        std::vector<SceneSnapshot> redoStack;
        bool wasUsingGizmo = false;
        void CopyRegistry(entt::registry &src, entt::registry &dst)
        {
            dst.clear();
            src.view<TagComponent>().each([&](entt::entity entity, TagComponent &tag)
                                          {
        entt::entity newEnt = dst.create(entity); 
        dst.emplace<TagComponent>(newEnt, tag);
    
        if (src.all_of<IDComponent>(entity)) 
            dst.emplace<IDComponent>(newEnt, src.get<IDComponent>(entity));
            
        if (src.all_of<TransformComponent>(entity)) {
            auto tComp = src.get<TransformComponent>(entity);
            tComp.transform.updatematrix = true;
            dst.emplace<TransformComponent>(newEnt, tComp);
        }
            
        if (src.all_of<MeshComponent>(entity)) 
            dst.emplace<MeshComponent>(newEnt, src.get<MeshComponent>(entity));
            
        if (src.all_of<LightComponent>(entity)) 
            dst.emplace<LightComponent>(newEnt, src.get<LightComponent>(entity));
            
        if (src.all_of<HierarchyComponent>(entity)) 
            dst.emplace<HierarchyComponent>(newEnt, src.get<HierarchyComponent>(entity)); });
        }
        void SaveState(entt::registry &registry)
        {
            auto snapReg = std::make_shared<entt::registry>();
            CopyRegistry(registry, *snapReg);
            undoStack.push_back({snapReg, selectedEntity});
            redoStack.clear();
            if (undoStack.size() > 50)
                undoStack.erase(undoStack.begin());
        }
        void Undo(entt::registry &registry)
        {
            if (undoStack.empty())
                return;
            auto snapReg = std::make_shared<entt::registry>();
            CopyRegistry(registry, *snapReg);
            redoStack.push_back({snapReg, selectedEntity});
            SceneSnapshot snap = undoStack.back();
            undoStack.pop_back();
            CopyRegistry(*snap.regCopy, registry);
            selectedEntity = snap.selectedEntity;
            if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity))
            {
                auto &t = registry.get<TransformComponent>(selectedEntity).transform;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
            }
        }
        void Redo(entt::registry &registry)
        {
            if (redoStack.empty())
                return;
            auto snapReg = std::make_shared<entt::registry>();
            CopyRegistry(registry, *snapReg);
            undoStack.push_back({snapReg, selectedEntity});
            SceneSnapshot snap = redoStack.back();
            redoStack.pop_back();
            CopyRegistry(*snap.regCopy, registry);
            selectedEntity = snap.selectedEntity;
            if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity))
            {
                auto &t = registry.get<TransformComponent>(selectedEntity).transform;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
            }
        }

        entt::entity FindEntityByID(entt::registry &registry, uint64_t id)
        {
            if (id == 0)
                return entt::null;
            auto view = registry.view<IDComponent>();
            for (auto e : view)
            {
                if (view.get<IDComponent>(e).ID == id)
                    return e;
            }
            return entt::null;
        }

        glm::mat4 GetGlobalTransform(entt::registry &registry, entt::entity entity)
        {
            if (!registry.valid(entity) || !registry.all_of<TransformComponent>(entity))
                return glm::mat4(1.0f);
            auto &tComp = registry.get<TransformComponent>(entity);
            tComp.transform.updateMatrixIfNeeded(); // Обновляем локальную перед чтением
            glm::mat4 globalMat = tComp.transform.matrix;

            if (registry.all_of<HierarchyComponent>(entity))
            {
                uint64_t parentID = registry.get<HierarchyComponent>(entity).parentID;
                entt::entity parentEnt = FindEntityByID(registry, parentID);
                if (parentEnt != entt::null)
                {
                    globalMat = GetGlobalTransform(registry, parentEnt) * globalMat;
                }
            }
            return globalMat;
        }

        void DetachFromParent(entt::registry &registry, entt::entity child)
        {
            if (!registry.all_of<HierarchyComponent>(child) || !registry.all_of<IDComponent>(child))
                return;
            auto &hc = registry.get<HierarchyComponent>(child);
            if (hc.parentID == 0)
                return;

            uint64_t myID = registry.get<IDComponent>(child).ID;
            entt::entity parentEnt = FindEntityByID(registry, hc.parentID);
            if (parentEnt != entt::null && registry.all_of<HierarchyComponent>(parentEnt))
            {
                auto &phc = registry.get<HierarchyComponent>(parentEnt);
                phc.childrenIDs.erase(std::remove(phc.childrenIDs.begin(), phc.childrenIDs.end(), myID), phc.childrenIDs.end());
            }
            hc.parentID = 0;
        }

        void AttachToParent(entt::registry &registry, entt::entity child, entt::entity newParent)
        {
            if (child == newParent)
                return;
            DetachFromParent(registry, child);
            if (newParent == entt::null)
                return;

            if (!registry.all_of<HierarchyComponent>(child))
                registry.emplace<HierarchyComponent>(child);
            if (!registry.all_of<HierarchyComponent>(newParent))
                registry.emplace<HierarchyComponent>(newParent);
            if (!registry.all_of<IDComponent>(child))
                registry.emplace<IDComponent>(child);
            if (!registry.all_of<IDComponent>(newParent))
                registry.emplace<IDComponent>(newParent);

            uint64_t myID = registry.get<IDComponent>(child).ID;
            uint64_t pid = registry.get<IDComponent>(newParent).ID;

            registry.get<HierarchyComponent>(child).parentID = pid;
            registry.get<HierarchyComponent>(newParent).childrenIDs.push_back(myID);
        }

        // Супер-помощник для создания объектов (чтобы не забыть IDComponent)
        entt::entity CreateBaseEntity(entt::registry &registry, const std::string &name)
        {
            entt::entity e = registry.create();
            registry.emplace<IDComponent>(e); // ТЕПЕРЬ ОН ЕСТЬ ВСЕГДА!
            registry.emplace<TagComponent>(e, name);
            registry.emplace<TransformComponent>(e);
            registry.emplace<HierarchyComponent>(e);
            return e;
        }

        entt::entity CloneHierarchy(entt::registry &registry, entt::entity source, entt::entity newParent)
        {
            entt::entity copy = registry.create();

            // 🔥 ГЕНЕРИРУЕМ УНИКАЛЬНЫЙ ID ДЛЯ КОПИИ
            registry.emplace<IDComponent>(copy);

            if (registry.all_of<TagComponent>(source))
            {
                auto tag = registry.get<TagComponent>(source);
                tag.name += " (Copy)";
                registry.emplace<TagComponent>(copy, tag);
            }
            if (registry.all_of<TransformComponent>(source))
                registry.emplace<TransformComponent>(copy, registry.get<TransformComponent>(source));
            if (registry.all_of<MeshComponent>(source))
                registry.emplace<MeshComponent>(copy, registry.get<MeshComponent>(source));
            if (registry.all_of<LightComponent>(source))
                registry.emplace<LightComponent>(copy, registry.get<LightComponent>(source));

            auto &hc = registry.emplace<HierarchyComponent>(copy);

            // Если мы передали родителя при дубликации, привязываем копию к нему
            if (newParent != entt::null && registry.all_of<IDComponent>(newParent))
            {
                hc.parentID = registry.get<IDComponent>(newParent).ID;
                registry.get<HierarchyComponent>(newParent).childrenIDs.push_back(registry.get<IDComponent>(copy).ID);
            }

            return copy;
        }
        void DeleteEntityRecursive(entt::registry &registry, entt::entity target)
        {
            if (!registry.valid(target))
                return;

            // 1. Сначала рекурсивно удаляем всех детей
            if (registry.all_of<HierarchyComponent>(target))
            {
                auto children = registry.get<HierarchyComponent>(target).childrenIDs; // Копируем!
                for (uint64_t childID : children)
                {
                    entt::entity childEnt = FindEntityByID(registry, childID);
                    DeleteEntityRecursive(registry, childEnt);
                }
            }

            // 2. Отвязываемся от родителя
            DetachFromParent(registry, target);

            // 3. Уничтожаем
            if (selectedEntity == target)
                selectedEntity = entt::null;
            registry.destroy(target);
        }
        void DeleteGameObject(entt::registry &registry, entt::entity target)
        {
            SaveState(registry);
            if (registry.all_of<HierarchyComponent>(target))
            {
            }
            DeleteEntityRecursive(registry, target);
        }
        bool IsDescendant(entt::registry &registry, entt::entity potentialChild, entt::entity potentialParent)
        {
            entt::entity curr = potentialChild;
            while (curr != entt::null && registry.all_of<HierarchyComponent>(curr))
            {
                if (curr == potentialParent)
                    return true;
            }
            return false;
        }
        struct RenderSettings
        {
            // --- Ray Tracing & GI ---
            int rtMaxBounces = 1;
            bool enableRTReflections = true;
            bool enableRadianceCascades = true;
            int rcProbeGridX = 16;
            int rcProbeGridY = 9;
            int rcProbeGridZ = 24;
            float rcBaseRayLength = 1.0f;
            int rcOctaSize = 8;

            bool enableSSAO = true;
            float ssaoRadius = 0.5f;
            float ssaoBias = 0.025f;
            float ssaoIntensity = 2.0f;
            float ssaoPower = 2.0f;
            bool enableSSGI = true;
            int ssgiRayCount = 8;
            float ssgiStepSize = 0.4f;
            float ssgiThickness = 0.5f;
            int blurRange = 4;
            float gamma = 2.2f;
            bool autoExposure = true;
            float manualExposure = 1.0f;
            float exposureCompensation = 1.0f;
            float minBrightness = 0.5f;
            float maxBrightness = 3.0f;
            float contrast = 1.0f;
            float saturation = 1.0f;
            bool enableVignette = false;
            float vignetteIntensity = 0.5f;
            bool enableChromaticAberration = false;
            float caIntensity = 0.005f;
            bool enableBloom = true;
            float bloomThreshold = 1.0f;
            float bloomIntensity = 1.5f;
            int bloomBlurIterations = 10;
            bool enableLensFlares = true;
            float flareIntensity = 0.5f;
            float ghostDispersal = 0.3f;
            int ghosts = 4;
            float currentExposure = 1.0f;
            float temperature = 8000.0f;
            bool enableDoF = false;
            float focusDistance = 10.0f;
            float focusRange = 3.0f;
            float bokehSize = 2.0f;
            bool enableMotionBlur = false;
            float mbStrength = 0.5f;
            bool enableGodRays = false;
            float godRaysIntensity = 1.0f;
            bool enableFilmGrain = false;
            float grainIntensity = 0.05f;
            bool enableSharpen = false;
            float sharpenIntensity = 0.5f;
            bool enableFog = true;
            float fogDensity = 0.02f;
            float fogHeightFalloff = 0.2f;
            float fogBaseHeight = 0.0f;
            float fogColor[3] = {0.5f, 0.6f, 0.7f};
            float inscatterColor[3] = {1.0f, 0.8f, 0.5f};
            float inscatterPower = 8.0f;
            float inscatterIntensity = 1.0f;
            float skyZenithColor[3] = {0.15f, 0.35f, 0.75f};
            float skyHorizonColor[3] = {0.6f, 0.7f, 0.8f};
            float sunSize = 0.005f;
            float sunGlow = 1.5f;
            float sunGlowSize = 0.1f;
            bool enableContactShadows = true;
            float contactShadowLength = 0.05f;
            float contactShadowThickness = 0.1f;
            int contactShadowSteps = 16;
        } renderSettings;
        void SetupBurnhopeTheme()
        {
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
        VkDescriptorPool imguiPool;
        ~UI()
        {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            vkDestroyDescriptorPool(m_device->device(), imguiPool, nullptr);
        }
        UI(BurnhopeWindow &window, burnhope::BurnhopeDevice &device, VkRenderPass renderPass, const std::string &projectPath, const std::string &exePath)
        {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuizmo::Enable(true);
            m_device = &device;
            ImGuiIO &io = ImGui::GetIO();
            (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            ImFontConfig font_cfg;
            font_cfg.OversampleH = 2;
            font_cfg.OversampleV = 2;
            SetupBurnhopeTheme();
            VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000;
            pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            vkCreateDescriptorPool(device.device(), &pool_info, nullptr, &imguiPool);
            ImGui_ImplGlfw_InitForVulkan(window.getGLFWwindow(), true);
            ImGui_ImplVulkan_InitInfo init_info = {};
            init_info.Instance = device.getInstance();
            init_info.PhysicalDevice = device.getPhysicalDevice();
            init_info.Device = device.device();
            init_info.QueueFamily = device.getGraphicsQueueFamily();
            init_info.Queue = device.graphicsQueue();
            init_info.DescriptorPool = imguiPool;
            init_info.MinImageCount = 2;
            init_info.ImageCount = 3;
            init_info.PipelineInfoMain.RenderPass = renderPass;
            init_info.PipelineInfoMain.Subpass = 0;
            init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering = false;
            ImGui_ImplVulkan_Init(&init_info);
            projectDirectory = projectPath;
            currentDirectory = projectPath;
            ExeDirectory = exePath;
            dirHistory.push_back(currentDirectory);
            dirHistoryIndex = 0;
            LoadRenderSettings();
        }
        std::string TruncateText(const std::string &text, float maxWidth)
        {
            if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
                return text;
            std::string res = text;
            while (res.length() > 0 && ImGui::CalcTextSize((res + "...").c_str()).x > maxWidth)
                res.pop_back();
            return res + "...";
        }
        std::string GetFileTypeName(const std::string &ext, bool isDir)
        {
            if (isDir)
                return "Folder";
            if (ext == ".bhmat")
                return "Material";
            if (ext == ".bhtex")
                return "Texture";
            if (ext == ".bhscene")
                return "Scene";
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                return "Image";
            if (ext == ".bhtex")
                return "Texture";
            if (ext == ".obj" || ext == ".fbx")
                return "Model";
            return "File";
        }
        void MoveToRecycleBin(const std::string &path)
        {
            try
            {
                std::filesystem::remove_all(path);
            }
            catch (...)
            {
            }
        }
        bool IsSelected(const std::string &path) { return std::find(selectedAssets.begin(), selectedAssets.end(), path) != selectedAssets.end(); }
        std::string GetPrimarySelection() { return selectedAssets.empty() ? "" : selectedAssets.back(); }
        void NavigateTo(const fs::path &target)
        {
            if (currentDirectory == target)
                return;
            if (dirHistoryIndex < dirHistory.size() - 1)
                dirHistory.erase(dirHistory.begin() + dirHistoryIndex + 1, dirHistory.end());
            dirHistory.push_back(target);
            dirHistoryIndex++;
            currentDirectory = target;
            selectedAssets.clear();
            renamingPath = "";
            lastClickedIndex = -1;
        }
        void StartRename(const std::string &path)
        {
            renamingPath = path;
            strcpy_s(inlineRenameBuf, sizeof(inlineRenameBuf), fs::path(path).stem().string().c_str());
            focusRename = true;
        }
        void ApplyRename()
        {
            if (!renamingPath.empty() && strlen(inlineRenameBuf) > 0)
            {
                fs::path oldP(renamingPath);
                std::string newName = std::string(inlineRenameBuf) + oldP.extension().string();
                fs::path newP = oldP.parent_path() / newName;
                if (oldP != newP && !fs::exists(newP))
                {
                    fs::rename(oldP, newP);
                    auto it = std::find(selectedAssets.begin(), selectedAssets.end(), renamingPath);
                    if (it != selectedAssets.end())
                        *it = newP.string();
                }
            }
            renamingPath = "";
        }
        void PasteCopiedItems(const fs::path &targetDir)
        {
            if (clipboardPaths.empty())
                return;
            for (const auto &cbPath : clipboardPaths)
            {
                if (!fs::exists(cbPath))
                    continue;
                fs::path src(cbPath);
                fs::path dst = targetDir / src.filename();
                int copyCount = 1;
                while (fs::exists(dst))
                {
                    dst = targetDir / (src.stem().string() + " " + std::to_string(copyCount) + src.extension().string());
                    copyCount++;
                }
                if (isCut)
                    fs::rename(src, dst);
                else
                    fs::copy(src, dst, fs::copy_options::recursive);
            }
            if (isCut)
            {
                clipboardPaths.clear();
                isCut = false;
            }
        }
        void LoadMaterialToProperties(const std::string &path)
        {
            memset(editAlbedo, 0, sizeof(editAlbedo));
            memset(editNormal, 0, sizeof(editNormal));
            memset(editHeight, 0, sizeof(editHeight));
            memset(editAO, 0, sizeof(editAO));
            memset(editMetallic, 0, sizeof(editMetallic));
            memset(editRoughness, 0, sizeof(editRoughness));
            memset(editEmissive, 0, sizeof(editEmissive));
            editEmissiveIntensity = 1.0f;
            std::ifstream file(path);
            if (file.is_open())
            {
                json j;
                try
                {
                    file >> j;
                }
                catch (...)
                {
                    return;
                }
                if (j.contains("textures"))
                {
                    if (j["textures"].contains("albedo"))
                        strcpy_s(editAlbedo, j["textures"]["albedo"].get<std::string>().c_str());
                    if (j["textures"].contains("normal"))
                        strcpy_s(editNormal, j["textures"]["normal"].get<std::string>().c_str());
                    if (j["textures"].contains("height"))
                        strcpy_s(editHeight, j["textures"]["height"].get<std::string>().c_str());
                    if (j["textures"].contains("ao"))
                        strcpy_s(editAO, j["textures"]["ao"].get<std::string>().c_str());
                    if (j["textures"].contains("metallic"))
                        strcpy_s(editMetallic, j["textures"]["metallic"].get<std::string>().c_str());
                    if (j["textures"].contains("roughness"))
                        strcpy_s(editRoughness, j["textures"]["roughness"].get<std::string>().c_str());
                    if (j["textures"].contains("emissive"))
                        strcpy_s(editEmissive, j["textures"]["emissive"].get<std::string>().c_str());
                }
                if (j.contains("emissiveIntensity")) {
                    editEmissiveIntensity = j["emissiveIntensity"].get<float>();
                }
            }
        }
        void DrawOutlinerNode(entt::registry &registry, entt::entity entity)
        {
            if (!registry.valid(entity))
                return;
            auto &tag = registry.get<TagComponent>(entity);

            bool isLeaf = true;
            if (registry.all_of<HierarchyComponent>(entity))
            {
                isLeaf = registry.get<HierarchyComponent>(entity).childrenIDs.empty();
            }

            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap;
            if (selectedEntity == entity)
                nodeFlags |= ImGuiTreeNodeFlags_Selected;
            if (isLeaf)
                nodeFlags |= ImGuiTreeNodeFlags_Leaf;

            bool nodeOpen = ImGui::TreeNodeEx((void *)(uintptr_t)entity, nodeFlags, tag.name.c_str());

            if (ImGui::IsItemClicked())
            {
                selectedEntity = entity;
            }

            // Меню по правому клику
            if (ImGui::BeginPopupContextItem())
            {
                selectedEntity = entity;
                if (ImGui::BeginMenu("Create Child..."))
                {
                    if (ImGui::MenuItem("Empty Object"))
                    {
                        SaveState(registry);
                        entt::entity newE = CreateBaseEntity(registry, "Empty");
                        AttachToParent(registry, newE, entity);
                    }
                    // Можешь добавить сюда Mesh и Light
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del"))
                {
                    DeleteGameObject(registry, entity);
                }
                ImGui::EndPopup();
            }

            // Drag & Drop
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGui::SetDragDropPayload("OUTLINER_NODE", &entity, sizeof(entt::entity));
                ImGui::Text("Move %s", tag.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE"))
                {
                    entt::entity dragged = *(const entt::entity *)payload->Data;
                    SaveState(registry);
                    AttachToParent(registry, dragged, entity);
                }
                ImGui::EndDragDropTarget();
            }

            // Рекурсивная отрисовка детей
            if (nodeOpen)
            {
                if (!isLeaf)
                {
                    auto children = registry.get<HierarchyComponent>(entity).childrenIDs;
                    for (uint64_t childID : children)
                    {
                        entt::entity childEnt = FindEntityByID(registry, childID);
                        if (childEnt != entt::null)
                            DrawOutlinerNode(registry, childEnt);
                    }
                }
                ImGui::TreePop();
            }
        }
        void DrawSceneOutliner(entt::registry &registry, ImGuiIO &io)
        {
            if (!showOutliner)
                return;
            ImGui::Begin("Scene Outliner", &showOutliner);

            if (ImGui::Button("+ Add", ImVec2(60, 25)))
                ImGui::OpenPopup("GlobalCreateMenu");
            ImGui::SameLine();
            if (ImGui::Button("Unparent", ImVec2(80, 25)) && selectedEntity != entt::null)
            {
                SaveState(registry);
                DetachFromParent(registry, selectedEntity);
            }

            if (ImGui::BeginPopup("GlobalCreateMenu"))
            {
                if (ImGui::MenuItem("Empty Object"))
                {
                    SaveState(registry);
                    CreateBaseEntity(registry, "Empty");
                }
                if (ImGui::MenuItem("Mesh Object"))
                {
                    SaveState(registry);
                    entt::entity e = CreateBaseEntity(registry, "Mesh");
                    registry.emplace<MeshComponent>(e);
                }
                if (ImGui::MenuItem("Light Source"))
                {
                    SaveState(registry);
                    entt::entity e = CreateBaseEntity(registry, "Light");
                    registry.emplace<LightComponent>(e);
                }
                ImGui::EndPopup();
            }
            ImGui::Separator();

            ImGui::BeginChild("OutlinerList", ImVec2(0, -20));

            // Отрисовываем ТОЛЬКО корневые объекты (у которых parentID == 0). Дети нарисуются сами.
            auto view = registry.view<IDComponent, TagComponent>();
            for (auto entity : view)
            {
                bool isRoot = true;
                if (registry.all_of<HierarchyComponent>(entity))
                {
                    if (registry.get<HierarchyComponent>(entity).parentID != 0)
                        isRoot = false;
                }
                if (isRoot)
                    DrawOutlinerNode(registry, entity);
            }

            ImGui::Dummy(ImGui::GetContentRegionAvail());
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE"))
                {
                    entt::entity dragged = *(const entt::entity *)payload->Data;
                    SaveState(registry);
                    DetachFromParent(registry, dragged); // Бросили в пустоту = отвязали
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::EndChild();
            ImGui::End();
        }
        void DrawSceneInspector(entt::registry &registry)
        {
            if (!showInspector)
                return;
            ImGui::Begin("Scene Inspector", &showInspector);
            if (selectedEntity == entt::null || !registry.valid(selectedEntity))
            {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an object in the scene");
                ImGui::
                    End();
                return;
            }
            auto &tag = registry.get<TagComponent>(selectedEntity);
            char nameBuf[128];
            strcpy_s(nameBuf, sizeof(nameBuf), tag.name.c_str());
            ImGui::PushItemWidth(-1);
            if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf)))
            {
                tag.name = nameBuf;
            }
            ImGui::PopItemWidth();
            ImGui::Spacing();
            if (registry.all_of<TransformComponent>(selectedEntity))
            {
                auto &tComp = registry.get<TransformComponent>(selectedEntity);
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool transformChanged = false;
                    if (ImGui::DragFloat3("Position", glm::value_ptr(tComp.transform.position), 0.1f))
                        transformChanged = true;
                    if (ImGui::IsItemActivated())
                        SaveState(registry);
                    if (ImGui::DragFloat3("Rotation", glm::value_ptr(tComp.transform.rotation), 1.0f))
                        transformChanged = true;
                    if (ImGui::IsItemActivated())
                        SaveState(registry);
                    if (ImGui::DragFloat3("Scale", glm::value_ptr(tComp.transform.scale), 0.05f))
                        transformChanged = true;
                    if (ImGui::IsItemActivated())
                        SaveState(registry);
                    if (transformChanged)
                    {
                        tComp.transform.updatematrix = true;
                        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(tComp.transform.position), glm::value_ptr(tComp.transform.rotation), glm::value_ptr(tComp.transform.scale), glm::value_ptr(model));
                    }
                }
            }
            if (registry.all_of<LightComponent>(selectedEntity))
            {
                auto &lComp = registry.get<LightComponent>(selectedEntity).light;
                bool removeLight = false;
                if (ImGui::CollapsingHeader("Light Component", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap))
                {
                    ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                    if (ImGui::Button("X##RM_LIGHT"))
                        removeLight = true;
                    ImGui::Checkbox("Enable Light", &lComp.enable);
                    const char *lightTypes[] = {"Directional", "Point", "Spot", "Rect", "Sky"};
                    int currentType = (int)lComp.type;
                    if (ImGui::Combo("Type", &currentType, lightTypes, IM_ARRAYSIZE(lightTypes)))
                    {
                        SaveState(registry);
                        lComp.type = (LightType)currentType;
                    }
                    const char *lightTypes2[] = {"Static", "Movable"};
                    int currentType2 = (int)lComp.mobility;
                    if (ImGui::Combo("Type Move", &currentType2, lightTypes2, IM_ARRAYSIZE(lightTypes2)))
                    {
                        SaveState(registry);
                        lComp.mobility = (LightMobility)currentType2;
                    }
                    ImGui::ColorEdit3("Color", glm::value_ptr(lComp.color));
                    ImGui::DragFloat("Intensity", &lComp.intensity, 0.1f, 0.0f, 1000.0f);
                    if (lComp.type == LightType::Point || lComp.type == LightType::Spot)
                        ImGui::DragFloat("Radius", &lComp.radius, 0.5f, 0.1f, 500.0f);
                    if (lComp.type == LightType::Spot)
                    {
                        ImGui::DragFloat("Inner Angle", &lComp.innerCone, 0.5f, 0.0f, lComp.outerCone);
                        ImGui::DragFloat("Outer Angle", &lComp.outerCone, 0.5f, lComp.innerCone, 90.0f);
                    }
                    ImGui::Checkbox("Cast Shadows", &lComp.castShadows);
                }
                if (removeLight)
                {
                    SaveState(registry);
                    registry.erase<LightComponent>(selectedEntity);
                }
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Add Component", ImVec2(-1, 30)))
                ImGui::OpenPopup("AddComponentPopup");
            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                if (!registry.all_of<MeshComponent>(selectedEntity) && ImGui::MenuItem("Mesh Renderer"))
                {
                    SaveState(registry);
                    registry.emplace<MeshComponent>(selectedEntity);
                }
                if (!registry.all_of<LightComponent>(selectedEntity) && ImGui::MenuItem("Light Component"))
                {
                    SaveState(registry);
                    registry.emplace<LightComponent>(selectedEntity);
                }
                ImGui::EndPopup();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("💾 SAVE SCENE", ImVec2(-1, 40)))
            {
                std::cout << "Need to update Serializer for EnTT!\n";
            }
            ImGui::End();
        }
        void DrawFolderTree(const fs::path &dir)
        {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (currentDirectory == dir)
                flags |= ImGuiTreeNodeFlags_Selected;
            bool isLeaf = true;
            for (auto &entry : fs::directory_iterator(dir))
            {
                if (entry.is_directory())
                {
                    isLeaf = false;
                    break;
                }
            }
            if (isLeaf)
                flags |= ImGuiTreeNodeFlags_Leaf;
            std::string nodeName = dir == projectDirectory ? "All (Project)" : dir.filename().string();
            bool isOpen = ImGui::TreeNodeEx(nodeName.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                NavigateTo(dir);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CB_ITEMS"))
                {
                    for (const auto &selPath : selectedAssets)
                    {
                        fs::path src(selPath);
                        if (src != dir)
                            fs::rename(src, dir / src.filename());
                    }
                    selectedAssets.clear();
                }
                ImGui::EndDragDropTarget();
            }
            if (isOpen)
            {
                for (auto &entry : fs::directory_iterator(dir))
                {
                    if (entry.is_directory())
                        DrawFolderTree(entry.path());
                }
                ImGui::TreePop();
            }
        }
        void DrawContentBrowser()
        {
            if (!showContentBrowser)
                return;
            ImGui::Begin("Content Browser", &showContentBrowser);
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                ImGuiIO &io = ImGui::GetIO();
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedAssets.empty() && renamingPath.empty())
                {
                    for (const auto &path : selectedAssets)
                        MoveToRecycleBin(path);
                    selectedAssets.clear();
                    lastClickedIndex = -1;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F2) && selectedAssets.size() == 1 && renamingPath.empty())
                {
                    StartRename(selectedAssets[0]);
                }
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedAssets.empty())
                {
                    clipboardPaths = selectedAssets;
                    isCut = false;
                }
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedAssets.empty())
                {
                    clipboardPaths = selectedAssets;
                    isCut = true;
                }
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboardPaths.empty())
                {
                    PasteCopiedItems(currentDirectory);
                }
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (dirHistoryIndex > 0)
            {
                if (ImGui::Button("<"))
                {
                    dirHistoryIndex--;
                    currentDirectory = dirHistory[dirHistoryIndex];
                    selectedAssets.clear();
                }
            }
            else
            {
                ImGui::TextDisabled("<");
            }
            ImGui::SameLine();
            if (dirHistoryIndex < dirHistory.size() - 1)
            {
                if (ImGui::Button(">"))
                {
                    dirHistoryIndex++;
                    currentDirectory = dirHistory[dirHistoryIndex];
                    selectedAssets.clear();
                }
            }
            else
            {
                ImGui::TextDisabled(">");
            }
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();
            ImGui::PopStyleColor();
            if (ImGui::Button(" + Create "))
                ImGui::OpenPopup("CreateMenuPopup");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputTextWithHint("##Search", "Search all folders...", searchBuffer, sizeof(searchBuffer));
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            if (ImGui::Button("All"))
                NavigateTo(projectDirectory);
            if (ImGui::BeginDragDropTarget())
            {
                if (ImGui::AcceptDragDropPayload("CB_ITEMS"))
                {
                    for (const auto &selPath : selectedAssets)
                    {
                        fs::path src(selPath);
                        if (src != projectDirectory)
                            fs::rename(src, projectDirectory / src.filename());
                    }
                    selectedAssets.clear();
                }
                ImGui::EndDragDropTarget();
            }
            fs::path rel = fs::relative(currentDirectory, projectDirectory);
            fs::path accum = projectDirectory;
            if (rel.string() != ".")
            {
                for (auto it = rel.begin(); it != rel.end(); ++it)
                {
                    ImGui::SameLine();
                    ImGui::Text(">");
                    ImGui::SameLine();
                    accum /= *it;
                    if (ImGui::Button(it->string().c_str()))
                        NavigateTo(accum);
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (ImGui::AcceptDragDropPayload("CB_ITEMS"))
                        {
                            for (const auto &selPath : selectedAssets)
                            {
                                fs::path src(selPath);
                                if (src != accum)
                                    fs::rename(src, accum / src.filename());
                            }
                            selectedAssets.clear();
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }
            ImGui::PopStyleColor();
            ImGui::Separator();
            if (ImGui::BeginPopup("CreateMenuPopup"))
            {
                if (ImGui::MenuItem("New Folder"))
                {
                    fs::path newPath = currentDirectory / "New Folder";
                    int count = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentDirectory / ("New Folder " + std::to_string(count));
                        count++;
                    }
                    fs::create_directory(newPath);
                    selectedAssets.clear();
                    selectedAssets.push_back(newPath.string());
                    StartRename(newPath.string());
                }
                if (ImGui::MenuItem("Material (.bhmat)"))
                {
                    fs::path newPath = currentDirectory / "New Material.bhmat";
                    int count = 1;
                    while (fs::exists(newPath))
                    {
                        newPath = currentDirectory / ("New Material " + std::to_string(count) + ".bhmat");
                        count++;
                    }
                    json j;
                    j["name"] = "New Material";
                    j["textures"] = {{"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""}};
                    std::ofstream file(newPath);
                    file << j.dump(4);
                    selectedAssets.clear();
                    selectedAssets.push_back(newPath.string());
                    LoadMaterialToProperties(newPath.string());
                    StartRename(newPath.string());
                }
                ImGui::EndPopup();
            }
            ImGui::Columns(2, "CB_Columns", true);
            if (ImGui::GetColumnWidth() == ImGui::GetContentRegionAvail().x)
                ImGui::SetColumnWidth(0, 200.0f);
            ImGui::SetColumnWidth(0, 200.0f);
            ImGui::BeginChild("LeftTreePanel");
            DrawFolderTree(projectDirectory);
            ImGui::EndChild();
            ImGui::NextColumn();
            ImGui::BeginChild("RightGridPanel");
            std::vector<fs::directory_entry> items;
            std::string searchStr(searchBuffer);
            std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
            if (!searchStr.empty())
            {
                for (auto &entry : fs::recursive_directory_iterator(projectDirectory))
                {
                    std::string lowerName = entry.path().filename().string();
                    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                    if (lowerName.find(searchStr) != std::string::npos)
                        items.push_back(entry);
                }
            }
            else
            {
                for (auto &entry : fs::directory_iterator(currentDirectory))
                {
                    items.push_back(entry);
                    if (entry.is_regular_file())
                    {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".fbx" || ext == ".obj")
                        {
                            fs::path compressedPath = entry.path();
                            compressedPath.replace_extension(".bhmesh");
                            if (!fs::exists(compressedPath))
                            {
                                std::cout << "Detected new model, compiling: " << entry.path().filename() << "...\n";
                                if (ModelImporter::ImportModel(entry.path().string(), compressedPath.string()))
                                {
                                    try
                                    {
                                        fs::remove(entry.path());
                                        std::cout << "Deleted original model: " << entry.path().filename() << "\n";
                                    }
                                    catch (...)
                                    {
                                    }
                                    continue;
                                }
                            }
                        }
                    }
                }
            }
            std::sort(items.begin(), items.end(), [](const fs::directory_entry &a, const fs::directory_entry &b)
                      {
            if (a.is_directory() && !b.is_directory()) return true;
            if (!a.is_directory() && b.is_directory()) return false;
            return a.path().filename().string() < b.path().filename().string(); });
            if (ImGui::BeginPopupContextWindow("CB_Bg_Context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            {
                if (ImGui::MenuItem("New Folder"))
                {
                    fs::path p = currentDirectory / "New Folder";
                    int c = 1;
                    while (fs::exists(p))
                    {
                        p = currentDirectory / ("New Folder " + std::to_string(c++));
                    }
                    fs::create_directory(p);
                    selectedAssets.clear();
                    selectedAssets.push_back(p.string());
                    StartRename(p.string());
                }
                if (ImGui::MenuItem("Material (.bhmat)"))
                {
                    fs::path p = currentDirectory / "New Material.bhmat";
                    int c = 1;
                    while (fs::exists(p))
                    {
                        p = currentDirectory / ("New Material " + std::to_string(c++) + ".bhmat");
                    }
                    json j;
                    j["name"] = "New Material";
                    j["textures"] = {{"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""}};
                    std::ofstream file(p);
                    file << j.dump(4);
                    selectedAssets.clear();
                    selectedAssets.push_back(p.string());
                    LoadMaterialToProperties(p.string());
                    StartRename(p.string());
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardPaths.empty()))
                {
                    PasteCopiedItems(currentDirectory);
                }
                ImGui::EndPopup();
            }
            float padding = 16.0f;
            float thumbnailSize = 64.0f;
            float itemWidth = thumbnailSize + 16.0f;
            float itemHeight = thumbnailSize + 45.0f;
            float cellSize = itemWidth + padding;
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = (int)(panelWidth / cellSize);
            if (columnCount < 1)
                columnCount = 1;
            ImGui::Columns(columnCount, 0, false);
            for (int i = 0; i < items.size(); ++i)
            {
                auto &entry = items[i];
                const auto &path = entry.path();
                std::string pathStr = path.string();
                std::string ext = path.extension().string();
                bool isDir = entry.is_directory();
                std::string nameNoExt = path.stem().string();
                std::string typeStr = GetFileTypeName(ext, isDir);
                ImGui::PushID(pathStr.c_str());
                float colWidth = ImGui::GetColumnWidth();
                float offsetX = (colWidth - itemWidth) / 2.0f;
                if (offsetX < 0)
                    offsetX = 0;
                ImVec2 startPos = ImGui::GetCursorScreenPos();
                ImVec2 itemPos = ImVec2(startPos.x + offsetX, startPos.y + padding / 2.0f);
                bool isSel = IsSelected(pathStr);
                bool isHovered = ImGui::IsMouseHoveringRect(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight));
                if (isSel)
                    ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(36, 112, 204, 150), 8.0f);
                else if (isHovered)
                    ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(60, 70, 85, 120), 8.0f);
                ImGui::SetCursorScreenPos(itemPos);
                ImGui::InvisibleButton("##hitbox", ImVec2(itemWidth, itemHeight));
                if (ImGui::IsItemHovered())
                {
                    if (ImGui::IsMouseClicked(0))
                    {
                        if (ImGui::GetIO().KeyCtrl)
                        {
                            if (isSel)
                                selectedAssets.erase(std::remove(selectedAssets.begin(), selectedAssets.end(), pathStr), selectedAssets.end());
                            else
                            {
                                selectedAssets.push_back(pathStr);
                                if (ext == ".bhmat")
                                    LoadMaterialToProperties(pathStr);
                            }
                            lastClickedIndex = i;
                        }
                        else if (ImGui::GetIO().KeyShift && lastClickedIndex != -1)
                        {
                            selectedAssets.clear();
                            int start = std::min(i, lastClickedIndex);
                            int end = std::max(i, lastClickedIndex);
                            for (int j = start; j <= end; j++)
                            {
                                selectedAssets.push_back(items[j].path().string());
                            }
                        }
                        else
                        {
                            if (!isSel)
                            {
                                selectedAssets.clear();
                                selectedAssets.push_back(pathStr);
                                if (ext == ".bhmat")
                                    LoadMaterialToProperties(pathStr);
                            }
                            lastClickedIndex = i;
                        }
                    }
                    if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0))
                    {
                        if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && isSel && selectedAssets.size() > 1)
                        {
                            selectedAssets.clear();
                            selectedAssets.push_back(pathStr);
                            if (ext == ".bhmat")
                                LoadMaterialToProperties(pathStr);
                        }
                    }
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        if (isDir)
                            NavigateTo(path);
                    }
                }
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    if (!isSel)
                    {
                        selectedAssets.clear();
                        selectedAssets.push_back(pathStr);
                    }
                    ImGui::SetDragDropPayload("CB_ITEMS", nullptr, 0);
                    ImGui::Text("Move %d items", (int)selectedAssets.size());
                    ImGui::EndDragDropSource();
                }
                if (isDir && ImGui::BeginDragDropTarget())
                {
                    if (ImGui::AcceptDragDropPayload("CB_ITEMS"))
                    {
                        for (const auto &selPath : selectedAssets)
                        {
                            fs::path src(selPath);
                            if (src != path)
                                fs::rename(src, path / src.filename());
                        }
                        selectedAssets.clear();
                    }
                    ImGui::EndDragDropTarget();
                }
                if (ImGui::BeginPopupContextItem("ItemContext"))
                {
                    if (!isSel)
                    {
                        selectedAssets.clear();
                        selectedAssets.push_back(pathStr);
                        if (ext == ".bhmat")
                            LoadMaterialToProperties(pathStr);
                    }
                    if (ImGui::MenuItem("Open"))
                    {
                        if (isDir)
                            NavigateTo(path);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Cut", "Ctrl+X"))
                    {
                        clipboardPaths = selectedAssets;
                        isCut = true;
                    }
                    if (ImGui::MenuItem("Copy", "Ctrl+C"))
                    {
                        clipboardPaths = selectedAssets;
                        isCut = false;
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Rename", "F2", false, selectedAssets.size() == 1))
                    {
                        StartRename(pathStr);
                    }
                    if (ImGui::MenuItem("Delete", "Del"))
                    {
                        for (auto &p : selectedAssets)
                            MoveToRecycleBin(p);
                        selectedAssets.clear();
                    }
                    ImGui::EndPopup();
                }
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + (itemWidth - thumbnailSize) / 2.0f, itemPos.y + 6.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.22f, 0.25f, 1.0f));
                ImGui::Button(isDir ? "DIR" : "FILE", ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor();
                if (renamingPath == pathStr)
                {
                    ImGui::SetCursorScreenPos(ImVec2(itemPos.x + 4, itemPos.y + thumbnailSize + 10.0f));
                    ImGui::SetNextItemWidth(itemWidth - 8);
                    if (focusRename)
                    {
                        ImGui::SetKeyboardFocusHere();
                        focusRename = false;
                        ImGui::SetScrollHereY();
                    }
                    if (ImGui::InputText("##rename", inlineRenameBuf, sizeof(inlineRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    {
                        ApplyRename();
                    }
                    if (!ImGui::IsItemActive() && !focusRename && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered())
                    {
                        ApplyRename();
                    }
                }
                else
                {
                    std::string truncName = TruncateText(nameNoExt, itemWidth - 8.0f);
                    float textOffset = (itemWidth - ImGui::CalcTextSize(truncName.c_str()).x) / 2.0f;
                    ImGui::SetCursorScreenPos(ImVec2(itemPos.x + textOffset, itemPos.y + thumbnailSize + 10.0f));
                    ImGui::Text("%s", truncName.c_str());
                    float typeOffset = (itemWidth - ImGui::CalcTextSize(typeStr.c_str()).x) / 2.0f;
                    ImGui::SetCursorScreenPos(ImVec2(itemPos.x + typeOffset, itemPos.y + thumbnailSize + 26.0f));
                    ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.0f), "%s", typeStr.c_str());
                }
                ImGui::NextColumn();
                ImGui::PopID();
            }
            ImGui::Columns(1);
            ImGui::EndChild();
            ImGui::Columns(1);
            ImGui::End();
        }
        void SaveRenderSettings()
        {
            std::string path = projectDirectory.string() + "/rendersettings.json";
            json j;
            j["rtMaxBounces"] = renderSettings.rtMaxBounces;
            j["enableRTReflections"] = renderSettings.enableRTReflections;
            j["enableRadianceCascades"] = renderSettings.enableRadianceCascades;
            j["rcProbeGridX"] = renderSettings.rcProbeGridX;
            j["rcProbeGridY"] = renderSettings.rcProbeGridY;
            j["rcProbeGridZ"] = renderSettings.rcProbeGridZ;
            j["rcBaseRayLength"] = renderSettings.rcBaseRayLength;
            j["rcOctaSize"] = renderSettings.rcOctaSize;

            j["enableSSAO"] = renderSettings.enableSSAO;
            j["ssaoRadius"] = renderSettings.ssaoRadius;
            j["ssaoBias"] = renderSettings.ssaoBias;
            j["ssaoIntensity"] = renderSettings.ssaoIntensity;
            j["ssaoPower"] = renderSettings.ssaoPower;
            j["enableSSGI"] = renderSettings.enableSSGI;
            j["ssgiRayCount"] = renderSettings.ssgiRayCount;
            j["ssgiStepSize"] = renderSettings.ssgiStepSize;
            j["ssgiThickness"] = renderSettings.ssgiThickness;
            j["blurRange"] = renderSettings.blurRange;
            j["autoExposure"] = renderSettings.autoExposure;
            j["manualExposure"] = renderSettings.manualExposure;
            j["exposureCompensation"] = renderSettings.exposureCompensation;
            j["minBrightness"] = renderSettings.minBrightness;
            j["maxBrightness"] = renderSettings.maxBrightness;
            j["contrast"] = renderSettings.contrast;
            j["saturation"] = renderSettings.saturation;
            j["temperature"] = renderSettings.temperature;
            j["gamma"] = renderSettings.gamma;
            j["enableVignette"] = renderSettings.enableVignette;
            j["vignetteIntensity"] = renderSettings.vignetteIntensity;
            j["enableChromaticAberration"] = renderSettings.enableChromaticAberration;
            j["caIntensity"] = renderSettings.caIntensity;
            j["enableBloom"] = renderSettings.enableBloom;
            j["bloomThreshold"] = renderSettings.bloomThreshold;
            j["bloomIntensity"] = renderSettings.bloomIntensity;
            j["bloomBlurIterations"] = renderSettings.bloomBlurIterations;
            j["enableLensFlares"] = renderSettings.enableLensFlares;
            j["flareIntensity"] = renderSettings.flareIntensity;
            j["ghostDispersal"] = renderSettings.ghostDispersal;
            j["ghosts"] = renderSettings.ghosts;
            j["enableGodRays"] = renderSettings.enableGodRays;
            j["godRaysIntensity"] = renderSettings.godRaysIntensity;
            j["enableFilmGrain"] = renderSettings.enableFilmGrain;
            j["grainIntensity"] = renderSettings.grainIntensity;
            j["enableSharpen"] = renderSettings.enableSharpen;
            j["sharpenIntensity"] = renderSettings.sharpenIntensity;
            j["enableDoF"] = renderSettings.enableDoF;
            j["focusDistance"] = renderSettings.focusDistance;
            j["focusRange"] = renderSettings.focusRange;
            j["bokehSize"] = renderSettings.bokehSize;
            j["enableMotionBlur"] = renderSettings.enableMotionBlur;
            j["mbStrength"] = renderSettings.mbStrength;
            j["enableFog"] = renderSettings.enableFog;
            j["fogDensity"] = renderSettings.fogDensity;
            j["fogHeightFalloff"] = renderSettings.fogHeightFalloff;
            j["fogBaseHeight"] = renderSettings.fogBaseHeight;
            j["inscatterPower"] = renderSettings.inscatterPower;
            j["inscatterIntensity"] = renderSettings.inscatterIntensity;
            j["fogColor"] = {renderSettings.fogColor[0], renderSettings.fogColor[1], renderSettings.fogColor[2]};
            j["inscatterColor"] = {renderSettings.inscatterColor[0], renderSettings.inscatterColor[1], renderSettings.inscatterColor[2]};
            j["skyZenithColor"] = {renderSettings.skyZenithColor[0], renderSettings.skyZenithColor[1], renderSettings.skyZenithColor[2]};
            j["skyHorizonColor"] = {renderSettings.skyHorizonColor[0], renderSettings.skyHorizonColor[1], renderSettings.skyHorizonColor[2]};
            j["sunSize"] = renderSettings.sunSize;
            j["sunGlow"] = renderSettings.sunGlow;
            j["sunGlowSize"] = renderSettings.sunGlowSize;
            std::ofstream file(path);
            file << j.dump(4);
        }
        void LoadRenderSettings()
        {
            std::string path = projectDirectory.string() + "/rendersettings.json";
            std::ifstream file(path);
            if (!file.is_open())
            {
                SaveRenderSettings();
                return;
            }
            json j;
            try
            {
                file >> j;
            }
            catch (...)
            {
                return;
            }
            auto loadFloat = [&](const char *key, float &val)
            { if (j.contains(key)) val = j[key]; };
            auto loadInt = [&](const char *key, int &val)
            { if (j.contains(key)) val = j[key]; };
            auto loadBool = [&](const char *key, bool &val)
            { if (j.contains(key)) val = j[key]; };

            loadInt("rtMaxBounces", renderSettings.rtMaxBounces);
            loadBool("enableRTReflections", renderSettings.enableRTReflections);
            loadBool("enableRadianceCascades", renderSettings.enableRadianceCascades);
            loadInt("rcProbeGridX", renderSettings.rcProbeGridX);
            loadInt("rcProbeGridY", renderSettings.rcProbeGridY);
            loadInt("rcProbeGridZ", renderSettings.rcProbeGridZ);
            loadFloat("rcBaseRayLength", renderSettings.rcBaseRayLength);
            loadInt("rcOctaSize", renderSettings.rcOctaSize);

            loadBool("enableSSAO", renderSettings.enableSSAO);
            loadFloat("ssaoRadius", renderSettings.ssaoRadius);
            loadFloat("ssaoBias", renderSettings.ssaoBias);
            loadFloat("ssaoIntensity", renderSettings.ssaoIntensity);
            loadFloat("ssaoPower", renderSettings.ssaoPower);
            loadBool("enableSSGI", renderSettings.enableSSGI);
            loadInt("ssgiRayCount", renderSettings.ssgiRayCount);
            loadFloat("ssgiStepSize", renderSettings.ssgiStepSize);
            loadFloat("ssgiThickness", renderSettings.ssgiThickness);
            loadInt("blurRange", renderSettings.blurRange);
            loadBool("autoExposure", renderSettings.autoExposure);
            loadFloat("manualExposure", renderSettings.manualExposure);
            loadFloat("exposureCompensation", renderSettings.exposureCompensation);
            loadFloat("minBrightness", renderSettings.minBrightness);
            loadFloat("maxBrightness", renderSettings.maxBrightness);
            loadFloat("contrast", renderSettings.contrast);
            loadFloat("saturation", renderSettings.saturation);
            loadFloat("temperature", renderSettings.temperature);
            loadFloat("gamma", renderSettings.gamma);
            loadBool("enableVignette", renderSettings.enableVignette);
            loadFloat("vignetteIntensity", renderSettings.vignetteIntensity);
            loadBool("enableChromaticAberration", renderSettings.enableChromaticAberration);
            loadFloat("caIntensity", renderSettings.caIntensity);
            loadBool("enableBloom", renderSettings.enableBloom);
            loadFloat("bloomThreshold", renderSettings.bloomThreshold);
            loadFloat("bloomIntensity", renderSettings.bloomIntensity);
            loadInt("bloomBlurIterations", renderSettings.bloomBlurIterations);
            loadBool("enableLensFlares", renderSettings.enableLensFlares);
            loadFloat("flareIntensity", renderSettings.flareIntensity);
            loadFloat("ghostDispersal", renderSettings.ghostDispersal);
            loadInt("ghosts", renderSettings.ghosts);
            loadBool("enableGodRays", renderSettings.enableGodRays);
            loadFloat("godRaysIntensity", renderSettings.godRaysIntensity);
            loadBool("enableFilmGrain", renderSettings.enableFilmGrain);
            loadFloat("grainIntensity", renderSettings.grainIntensity);
            loadBool("enableSharpen", renderSettings.enableSharpen);
            loadFloat("sharpenIntensity", renderSettings.sharpenIntensity);
            loadBool("enableDoF", renderSettings.enableDoF);
            loadFloat("focusDistance", renderSettings.focusDistance);
            loadFloat("focusRange", renderSettings.focusRange);
            loadFloat("bokehSize", renderSettings.bokehSize);
            loadBool("enableMotionBlur", renderSettings.enableMotionBlur);
            loadFloat("mbStrength", renderSettings.mbStrength);
            loadBool("enableFog", renderSettings.enableFog);
            loadFloat("fogDensity", renderSettings.fogDensity);
            loadFloat("fogHeightFalloff", renderSettings.fogHeightFalloff);
            loadFloat("fogBaseHeight", renderSettings.fogBaseHeight);
            loadFloat("inscatterPower", renderSettings.inscatterPower);
            loadFloat("inscatterIntensity", renderSettings.inscatterIntensity);
            if (j.contains("fogColor") && j["fogColor"].is_array())
            {
                for (int i = 0; i < 3; i++)
                    renderSettings.fogColor[i] = j["fogColor"][i];
            }
            if (j.contains("inscatterColor") && j["inscatterColor"].is_array())
            {
                for (int i = 0; i < 3; i++)
                    renderSettings.inscatterColor[i] = j["inscatterColor"][i];
            }
        }
        void DrawRenderContent()
        {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Render & Post-Processing Settings");
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::BeginTabBar("PP_Tabs"))
            {
                if (ImGui::BeginTabItem("Global Illumination"))
                {
                    if (ImGui::CollapsingHeader("SSGI (Global Illumination)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable SSGI", &renderSettings.enableSSGI);
                        if (renderSettings.enableSSGI)
                        {
                            ImGui::SliderInt("Ray Count", &renderSettings.ssgiRayCount, 1, 32);
                            ImGui::SliderFloat("Step Size", &renderSettings.ssgiStepSize, 0.05f, 2.0f);
                            ImGui::SliderFloat("Thickness", &renderSettings.ssgiThickness, 0.01f, 2.0f);
                            ImGui::SliderInt("Blur Range", &renderSettings.blurRange, 1, 10);
                        }
                    }
                    if (ImGui::CollapsingHeader("Radiance Cascades (RT GI)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable RT GI", &renderSettings.enableRadianceCascades);
                        if (renderSettings.enableRadianceCascades)
                        {
                            ImGui::SliderInt("Probe Grid X", &renderSettings.rcProbeGridX, 1, 32);
                            ImGui::SliderInt("Probe Grid Y", &renderSettings.rcProbeGridY, 1, 32);
                            ImGui::SliderInt("Probe Grid Z", &renderSettings.rcProbeGridZ, 1, 32);
                            ImGui::SliderFloat("Ray Length", &renderSettings.rcBaseRayLength, 0.1f, 5.0f);
                            ImGui::SliderInt("Octahedron Size", &renderSettings.rcOctaSize, 4, 16);
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Reflections"))
                {
                    if (ImGui::CollapsingHeader("Ray Traced Reflections", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable RT Reflections", &renderSettings.enableRTReflections);
                        if (renderSettings.enableRTReflections)
                        {
                            ImGui::SliderInt("Max Bounces", &renderSettings.rtMaxBounces, 1, 5);
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Shadows & AO"))
                {
                    if (ImGui::CollapsingHeader("GTAO (Ambient Occlusion)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable GTAO", &renderSettings.enableSSAO);
                        if (renderSettings.enableSSAO)
                        {
                            ImGui::SliderFloat("Radius##ssao", &renderSettings.ssaoRadius, 0.1f, 3.0f);
                            ImGui::SliderFloat("Bias##ssao", &renderSettings.ssaoBias, 0.001f, 0.2f);
                            ImGui::SliderFloat("Intensity##ssao", &renderSettings.ssaoIntensity, 0.1f, 10.0f);
                            ImGui::SliderFloat("Power##ssao", &renderSettings.ssaoPower, 1.0f, 8.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("SSCS (Contact Shadows)", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable SSCS", &renderSettings.enableContactShadows);
                        if (renderSettings.enableContactShadows)
                        {
                            ImGui::SliderFloat("Ray Length", &renderSettings.contactShadowLength, 0.01f, 0.5f);
                            ImGui::SliderInt("Ray Steps", &renderSettings.contactShadowSteps, 4, 64);
                            ImGui::SliderFloat("Ray Thickness", &renderSettings.contactShadowThickness, 0.01f, 0.5f);
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Camera & Lens"))
                {
                    if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Auto Exposure", &renderSettings.autoExposure);
                        if (renderSettings.autoExposure)
                        {
                            ImGui::SliderFloat("Compensation", &renderSettings.exposureCompensation, 0.1f, 5.0f);
                            ImGui::SliderFloat("Min Brightness", &renderSettings.minBrightness, 0.01f, 2.0f);
                            ImGui::SliderFloat("Max Brightness", &renderSettings.maxBrightness, 1.0f, 10.0f);
                        }
                        else
                        {
                            ImGui::SliderFloat("Manual Exp", &renderSettings.manualExposure, 0.1f, 10.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable DoF", &renderSettings.enableDoF);
                        if (renderSettings.enableDoF)
                        {
                            ImGui::SliderFloat("Focus Dist", &renderSettings.focusDistance, 0.1f, 100.0f);
                            ImGui::SliderFloat("Focus Range", &renderSettings.focusRange, 0.1f, 50.0f);
                            ImGui::SliderFloat("Bokeh Size", &renderSettings.bokehSize, 0.0f, 10.0f);
                        }
                    }
                    if (ImGui::CollapsingHeader("Bloom & Lens Flares", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable Bloom", &renderSettings.enableBloom);
                        if (renderSettings.enableBloom)
                        {
                            ImGui::SliderFloat("Threshold##bloom", &renderSettings.bloomThreshold, 0.0f, 5.0f);
                            ImGui::SliderFloat("Intensity##bloom", &renderSettings.bloomIntensity, 0.0f, 5.0f);
                            ImGui::SliderInt("Blur Iterations", &renderSettings.bloomBlurIterations, 1, 15);
                        }
                        ImGui::Separator();
                        ImGui::Checkbox("Enable Lens Flares", &renderSettings.enableLensFlares);
                        if (renderSettings.enableLensFlares)
                        {
                            ImGui::SliderFloat("Flare Intensity", &renderSettings.flareIntensity, 0.0f, 5.0f);
                            ImGui::SliderFloat("Ghost Dispersal", &renderSettings.ghostDispersal, 0.01f, 1.0f);
                            ImGui::SliderInt("Ghosts Count", &renderSettings.ghosts, 1, 10);
                        }
                    }
                    if (ImGui::CollapsingHeader("Motion Blur", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable Motion Blur", &renderSettings.enableMotionBlur);
                        if (renderSettings.enableMotionBlur)
                            ImGui::SliderFloat("Strength", &renderSettings.mbStrength, 0.0f, 2.0f);
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Environment"))
                {
                    if (ImGui::CollapsingHeader("Procedural Sky", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::ColorEdit3("Zenith Color", renderSettings.skyZenithColor);
                        ImGui::ColorEdit3("Horizon Color", renderSettings.skyHorizonColor);
                        ImGui::SliderFloat("Sun Size", &renderSettings.sunSize, 0.001f, 0.1f);
                        ImGui::SliderFloat("Sun Glow", &renderSettings.sunGlow, 0.0f, 10.0f);
                        ImGui::SliderFloat("Sun Glow Size", &renderSettings.sunGlowSize, 0.01f, 1.0f);
                    }
                    if (ImGui::CollapsingHeader("Atmospheric Fog", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Enable Fog", &renderSettings.enableFog);
                        if (renderSettings.enableFog)
                        {
                            ImGui::ColorEdit3("Fog Color", renderSettings.fogColor);
                            ImGui::ColorEdit3("Sun Inscatter Color", renderSettings.inscatterColor);
                            ImGui::SliderFloat("Density", &renderSettings.fogDensity, 0.001f, 0.2f);
                            ImGui::SliderFloat("Height Falloff", &renderSettings.fogHeightFalloff, 0.01f, 1.0f);
                            ImGui::SliderFloat("Base Height", &renderSettings.fogBaseHeight, -50.0f, 50.0f);
                            ImGui::SliderFloat("Sun Inscatter Power", &renderSettings.inscatterPower, 1.0f, 32.0f);
                            ImGui::SliderFloat("Sun Inscatter Int", &renderSettings.inscatterIntensity, 0.0f, 5.0f);
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Color Grading"))
                {
                    if (ImGui::CollapsingHeader("Color Corrections", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::SliderFloat("Contrast", &renderSettings.contrast, 0.5f, 2.0f);
                        ImGui::SliderFloat("Saturation", &renderSettings.saturation, 0.0f, 2.0f);
                        ImGui::SliderFloat("Color Temp (K)", &renderSettings.temperature, 2000.0f, 12000.0f);
                        ImGui::SliderFloat("Gamma", &renderSettings.gamma, 1.0f, 2.8f);
                    }
                    if (ImGui::CollapsingHeader("Screen FX", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        ImGui::Checkbox("Film Grain", &renderSettings.enableFilmGrain);
                        if (renderSettings.enableFilmGrain)
                            ImGui::SliderFloat("Grain Strength", &renderSettings.grainIntensity, 0.0f, 0.2f);
                        ImGui::Checkbox("Vignette", &renderSettings.enableVignette);
                        if (renderSettings.enableVignette)
                            ImGui::SliderFloat("Vignette Intensity", &renderSettings.vignetteIntensity, 0.1f, 2.0f);
                        ImGui::Checkbox("Chromatic Aberration", &renderSettings.enableChromaticAberration);
                        if (renderSettings.enableChromaticAberration)
                            ImGui::SliderFloat("CA Intensity", &renderSettings.caIntensity, 0.001f, 0.55f);
                        ImGui::Checkbox("Enable Sharpen", &renderSettings.enableSharpen);
                        if (renderSettings.enableSharpen)
                            ImGui::SliderFloat("Sharpness", &renderSettings.sharpenIntensity, 0.0f, 2.0f);
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("💾 SAVE SETTINGS", ImVec2(-1, 40)))
                SaveRenderSettings();
        }
        void DrawTextureProperty(const char *label, char *pathBuffer, size_t bufferSize)
        {
            ImGui::PushID(label);
            ImGui::Text("%s", label);
            ImGui::Button("NO TEX", ImVec2(64, 64));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::TextWrapped("%s", strlen(pathBuffer) > 0 ? pathBuffer : "None");
            if (ImGui::Button("Select Texture..."))
                ImGui::OpenPopup("TexturePickerPopup");
            ImGui::EndGroup();
            if (ImGui::BeginPopup("TexturePickerPopup"))
            {
                ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Textures:");
                ImGui::Separator();
                for (auto &entry : fs::recursive_directory_iterator(projectDirectory))
                {
                    if (entry.is_regular_file())
                    {
                        std::string ext = entry.path().extension().string();
                        if (ext == ".bhtex")
                        {
                            std::string relPath = fs::relative(entry.path(), projectDirectory).string();
                            std::replace(relPath.begin(), relPath.end(), '\\', '/');
                            if (ImGui::Selectable(relPath.c_str()))
                            {
                                strcpy_s(pathBuffer, bufferSize, relPath.c_str());
                            }
                        }
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        void DrawPropertiesWindow()
        {
            if (!showProperties) return;
            ImGui::Begin("Properties", &showProperties);
            
            if (ImGui::BeginTabBar("PropsTabs")) {
                if (ImGui::BeginTabItem("Render")) {
                    DrawRenderContent();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::End();
        }
        bool DrawAssetPicker(const char *label, std::string &outPath, const std::vector<std::string> &extensions)
        {
            return false;
        }
        bool TestRayOBB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::mat4 modelMatrix, float &tOutput)
        {
            glm::mat4 invModel = glm::inverse(modelMatrix);
            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));
            float tMin = -100000.0f;
            float tMax = 100000.0f;
            glm::vec3 boxMin = glm::vec3(-1.0f);
            glm::vec3 boxMax = glm::vec3(1.0f);
            for (int i = 0; i < 3; i++)
            {
                float invD = 1.0f / localDir[i];
                float t1 = (boxMin[i] - localOrigin[i]) * invD;
                float t2 = (boxMax[i] - localOrigin[i]) * invD;
                if (invD < 0.0f)
                    std::swap(t1, t2);
                tMin = t1 > tMin ? t1 : tMin;
                tMax = t2 < tMax ? t2 : tMax;
                if (tMin > tMax)
                    return false;
            }
            tOutput = tMin;
            return tMax > 0;
        }
        glm::vec3 GetMouseRay(BurnhopeWindow &window, Camera &camera)
        {
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
        void DrawMainMenuBar(entt::registry &registry)
        {
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::MenuItem("New Scene"))
                    {
                        registry.clear();
                        selectedEntity = entt::null;
                        undoStack.clear();
                        redoStack.clear();
                    }
                    if (ImGui::MenuItem("Save Scene"))
                    {
                        std::cout << "Update Serializer!\n";
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty()))
                    {
                        Undo(registry);
                    }
                    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !redoStack.empty()))
                    {
                        Redo(registry);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Window"))
                {
                    ImGui::MenuItem("Scene Outliner", NULL, &showOutliner);
                    ImGui::MenuItem("Scene Inspector", NULL, &showInspector);
                    ImGui::MenuItem("Properties", NULL, &showProperties);
                    ImGui::MenuItem("Content Browser", NULL, &showContentBrowser);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Restore Defaults"))
                    {
                        resetLayout = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
        }
        void Draw(BurnhopeWindow &window, Camera &camera, entt::registry &registry, VkCommandBuffer commandBuffer)
        {
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            DrawMainMenuBar(registry);
            ImGuiIO &io = ImGui::GetIO();
            if (!ImGui::IsAnyItemActive())
            {
                if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
                    Undo(registry);
                if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
                    Redo(registry);
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedEntity != entt::null)
                {
                    SaveState(registry);
                    entt::entity parent = entt::null;
                    entt::entity newEnt = CloneHierarchy(registry, selectedEntity, parent);
                    selectedEntity = newEnt;
                }
            }
            ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
            ImGui::Begin("MainDockSpace_Window", nullptr, window_flags);
            ImGui::PopStyleVar(3);
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
            static bool first_time = true;
            if (first_time || resetLayout)
            {
                first_time = false;
                resetLayout = false;
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
            ImGuizmo::BeginFrame();
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
            auto extent = window.getExtent();
            ImGuizmo::SetRect(0, 0, (float)extent.width, (float)extent.height);
            static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
            static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
            if (ImGui::IsKeyPressed(ImGuiKey_Q))
                currentGizmoOperation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E))
                currentGizmoOperation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R))
                currentGizmoOperation = ImGuizmo::SCALE;
            glm::mat4 view = camera.GetViewMatrix();
            glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);
            glm::mat4 gizmoProj = proj;
            gizmoProj[1][1] *= -1.0f;
            if (ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered() && !ImGuizmo::IsOver())
            {
                glm::vec3 rayOrigin = camera.Position;
                glm::vec3 rayDir = GetMouseRay(window, camera);
                float closestT = 1000.0f;
                entt::entity hitIndex = entt::null;
                auto pickView = registry.view<TransformComponent>();
                pickView.each([&](entt::entity entity, TransformComponent &tComp)
                              {
                float t;
                if (TestRayOBB(rayOrigin, rayDir, tComp.transform.matrix, t)) {
                    if (t < closestT) { closestT = t; hitIndex = entity; }
                } });
                if (hitIndex != entt::null)
                {
                    selectedEntity = hitIndex;
                    ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.position), glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.rotation), glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.scale), glm::value_ptr(model));
                }
            }
            if (selectedEntity != entt::null && registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity))
            {
                auto &tComp = registry.get<TransformComponent>(selectedEntity);
                tComp.transform.updateMatrixIfNeeded(); // ОБЯЗАТЕЛЬНО: актуализируем локальную матрицу

                // 1. Вычисляем мировую матрицу РОДИТЕЛЯ (если он есть)
                glm::mat4 parentWorldMatrix = glm::mat4(1.0f);
                if (registry.all_of<HierarchyComponent>(selectedEntity))
                {
                    uint64_t pid = registry.get<HierarchyComponent>(selectedEntity).parentID;
                    entt::entity pEnt = FindEntityByID(registry, pid);
                    if (pEnt != entt::null)
                    {
                        parentWorldMatrix = GetGlobalTransform(registry, pEnt);
                    }
                }

                // 2. Итоговая мировая матрица этого объекта
                glm::mat4 worldMatrix = parentWorldMatrix * tComp.transform.matrix;

                if (ImGuizmo::IsUsing() && !wasUsingGizmo)
                    SaveState(registry);
                wasUsingGizmo = ImGuizmo::IsUsing();

                // 3. Отдаем Гизмо именно МИРОВУЮ матрицу для отрисовки и манипуляции
                ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(gizmoProj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(worldMatrix));

                // 4. Если мы потянули за стрелочку:
                if (ImGuizmo::IsUsing())
                {
                    // Вычитаем из новой мировой матрицы влияние родителя, чтобы получить новую ЛОКАЛЬНУЮ матрицу
                    glm::mat4 newLocalMatrix = glm::inverse(parentWorldMatrix) * worldMatrix;

                    // Разбиваем локальную матрицу на компоненты
                    ImGuizmo::DecomposeMatrixToComponents(
                        glm::value_ptr(newLocalMatrix),
                        glm::value_ptr(tComp.transform.position),
                        glm::value_ptr(tComp.transform.rotation),
                        glm::value_ptr(tComp.transform.scale));
                    tComp.transform.updatematrix = true; // Ставим флаг, чтобы в следующем кадре матрица пересобралась
                }
            }
            DrawSceneOutliner(registry, io);
            DrawSceneInspector(registry);
            DrawContentBrowser();
            DrawPropertiesWindow();
            ImGui::Render();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        }
    };
}
#endif