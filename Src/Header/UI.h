#ifndef UI_CLASS_H
#define UI_CLASS_H

#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glad/glad.h>
#include <vector>
#include <GLFW/glfw3.h>
#include <iostream>
#include "Serializer.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h" 
#include "ImGuizmo.h"
#include "Window.h"
#include "Components.h" // НАШ НОВЫЙ ФАЙЛ С КОМПОНЕНТАМИ!
#include "Camera.h"
#include "Render.h"
#include <cstdlib>
#include <filesystem>
#include <unordered_map>
#include <fstream>
#include <glm/gtx/intersect.hpp>
#include <nlohmann/json.hpp>
#include "stb_image.h" 
#include <algorithm> 
#include <windows.h>
#include <shellapi.h>
#include <memory>
#include "TextureImporter.h"
#include "ModelImporter.h"

class Render;
using json = nlohmann::json;
namespace fs = std::filesystem;

class UI {
public:
    bool isSceneUnsaved = false; // Отслеживает, есть ли изменения
    bool showExitPrompt = false; // Нужно ли показать окошко
    bool readyToExit = false;    // Точно ли мы готовы выйти

    // Переменные для контроля состояния
    bool isPlaying = false;
    entt::registry backupRegistry; // Здесь будет храниться копия сцены
    // Переменные для окошка Сцены
    ImVec2 viewportSize = ImVec2(0, 0);
    ImVec2 viewportBounds[2] = { ImVec2(0, 0), ImVec2(0, 0) };
    bool isViewportHovered = false;
    // Шаблонная магия C++17: копирует любые компоненты из одного реестра в другой
 // Переменные для хранения "слепка" до запуска игры
    std::map<entt::entity, TransformComponent> backupTransforms;
    std::map<entt::entity, PhysicsComponent> backupPhysics;

    void SaveSceneState(entt::registry& registry) {
        backupTransforms.clear();
        backupPhysics.clear();

        // Запоминаем позиции и настройки физики для КАЖДОГО объекта
        for (auto [entity] : registry.storage<entt::entity>().each()) {
            if (registry.all_of<TransformComponent>(entity)) {
                backupTransforms[entity] = registry.get<TransformComponent>(entity);
            }
            if (registry.all_of<PhysicsComponent>(entity)) {
                backupPhysics[entity] = registry.get<PhysicsComponent>(entity);
            }
        }
    }

    void RestoreSceneState(entt::registry& registry) {
        // 1. Если во время игры родились новые объекты (например, пули) - удаляем их
        std::vector<entt::entity> toDelete;
        for (auto [entity] : registry.storage<entt::entity>().each()) {
            if (backupTransforms.find(entity) == backupTransforms.end()) {
                toDelete.push_back(entity);
            }
        }
        for (auto e : toDelete) registry.destroy(e);

        // 2. Возвращаем старые координаты и физику всем изначальным объектам!
        for (auto& [entity, tComp] : backupTransforms) {
            if (registry.valid(entity)) {
                registry.emplace_or_replace<TransformComponent>(entity, tComp);
            }
        }
        for (auto& [entity, pComp] : backupPhysics) {
            if (registry.valid(entity)) {
                registry.emplace_or_replace<PhysicsComponent>(entity, pComp);
                // Пингуем Jolt Physics, чтобы он телепортировал коллайдеры обратно наверх
                registry.get<PhysicsComponent>(entity).updatePhysicsTransform = true;

            }
        }
    }

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
    char editAlbedo[256] = ""; char editNormal[256] = ""; char editMetallic[256] = "";
    char editRoughness[256] = ""; char editHeight[256] = ""; char editAO[256] = "";
    std::unordered_map<std::string, GLuint> imageThumbnails;
    bool showOutliner = true; bool showInspector = true;
    bool showProperties = true; bool showContentBrowser = true;
    bool resetLayout = false; bool showAboutModal = false;

    // --- СИСТЕМА СОХРАНЕНИЯ СОСТОЯНИЙ (ДЛЯ ENTT) ---
    struct SceneSnapshot {
        std::shared_ptr<entt::registry> regCopy;
        entt::entity selectedEntity;
    };
    std::vector<SceneSnapshot> undoStack;
    std::vector<SceneSnapshot> redoStack;
    bool wasUsingGizmo = false;

    // Хелпер для полного копирования реестра (нужен для Undo/Redo)
    void CopyRegistry(entt::registry& src, entt::registry& dst) {
        dst.clear();
        // Используем безопасный обход по TagComponent
        src.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
            entt::entity newEnt = dst.create(entity); // Создаем с тем же ID
            dst.emplace<TagComponent>(newEnt, tag);
            if (src.all_of<TransformComponent>(entity)) dst.emplace<TransformComponent>(newEnt, src.get<TransformComponent>(entity));
            if (src.all_of<MeshComponent>(entity)) dst.emplace<MeshComponent>(newEnt, src.get<MeshComponent>(entity));
            if (src.all_of<LightComponent>(entity)) dst.emplace<LightComponent>(newEnt, src.get<LightComponent>(entity));
            if (src.all_of<HierarchyComponent>(entity)) dst.emplace<HierarchyComponent>(newEnt, src.get<HierarchyComponent>(entity));
            if (src.all_of<PhysicsComponent>(entity)) dst.emplace<PhysicsComponent>(newEnt, src.get<PhysicsComponent>(entity));
            });
    }

    void SaveState(entt::registry& registry) {
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        undoStack.push_back({ snapReg, selectedEntity });
        redoStack.clear();
        if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
    }

    void Undo(entt::registry& registry) {
        if (undoStack.empty()) return;
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        redoStack.push_back({ snapReg, selectedEntity });

        SceneSnapshot snap = undoStack.back();
        undoStack.pop_back();
        CopyRegistry(*snap.regCopy, registry);
        selectedEntity = snap.selectedEntity;

        if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& t = registry.get<TransformComponent>(selectedEntity).transform;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
        }
    }

    void Redo(entt::registry& registry) {
        if (redoStack.empty()) return;
        auto snapReg = std::make_shared<entt::registry>();
        CopyRegistry(registry, *snapReg);
        undoStack.push_back({ snapReg, selectedEntity });

        SceneSnapshot snap = redoStack.back();
        redoStack.pop_back();
        CopyRegistry(*snap.regCopy, registry);
        selectedEntity = snap.selectedEntity;

        if (registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& t = registry.get<TransformComponent>(selectedEntity).transform;
            ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
        }
    }

    // --- ИЕРАРХИЯ И УДАЛЕНИЕ ---
    entt::entity CloneHierarchy(entt::registry& registry, entt::entity source, entt::entity newParent) {
        entt::entity copy = registry.create();

        if (registry.all_of<TagComponent>(source)) {
            auto tag = registry.get<TagComponent>(source);
            tag.name += " (Copy)";
            registry.emplace<TagComponent>(copy, tag);
        }
        if (registry.all_of<TransformComponent>(source)) registry.emplace<TransformComponent>(copy, registry.get<TransformComponent>(source));
        if (registry.all_of<MeshComponent>(source)) registry.emplace<MeshComponent>(copy, registry.get<MeshComponent>(source));
        if (registry.all_of<LightComponent>(source)) registry.emplace<LightComponent>(copy, registry.get<LightComponent>(source));
        if (registry.all_of<PhysicsComponent>(source)) {
            auto phys = registry.get<PhysicsComponent>(source);
            phys.rebuildPhysics = true;
            registry.emplace<PhysicsComponent>(copy, phys);
        }

        auto& hc = registry.emplace<HierarchyComponent>(copy);
        hc.parent = newParent;

        if (registry.all_of<HierarchyComponent>(source)) {
            for (entt::entity child : registry.get<HierarchyComponent>(source).children) {
                entt::entity newChild = CloneHierarchy(registry, child, copy);
                hc.children.push_back(newChild);
            }
        }
        return copy;
    }

    void DeleteEntityRecursive(entt::registry& registry, entt::entity target) {
        if (registry.all_of<HierarchyComponent>(target)) {
            // Копируем массив детей, так как мы будем их удалять
            auto children = registry.get<HierarchyComponent>(target).children;
            for (entt::entity child : children) {
                DeleteEntityRecursive(registry, child);
            }
        }
        if (selectedEntity == target) selectedEntity = entt::null;
        registry.destroy(target);
    }
    // --- ХЕЛПЕРЫ ДЛЯ КРАСИВОГО ИНСПЕКТОРА (КАК В UNREAL) ---

    // Начинаем табличку из 3 колонок: Название | Значение | Кнопка сброса
    bool BeginInspectorTable(const char* tableId) {
        ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable(tableId, 3, flags)) {
            // Настраиваем колонки: первая под текст, вторая тянется, третья узкая для кнопки
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed, 22.0f);
            return true;
        }
        return false;
    }

    // Вектор из трех чисел (например, позиция)
    bool DrawPropertyVec3(const char* label, glm::vec3& values, const glm::vec3& resetValue = glm::vec3(0.0f)) {
        bool changed = false;
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN); // Растягиваем на всю ширину
        if (ImGui::DragFloat3("##v", glm::value_ptr(values), 0.1f)) changed = true;

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) {
            values = resetValue;
            changed = true;

        }
        ImGui::PopID();
        return changed;
    }
    void DrawToolbar(entt::registry& registry, Render& render) {
        // Убираем отступы, чтобы панелька была аккуратной
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

        // Создаем окно сверху (высота 40 пикселей)
        ImGui::Begin("##Toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        float buttonSize = 32.0f;
        // Центрируем кнопки
        ImGui::SetCursorPosX((ImGui::GetWindowContentRegionMax().x * 0.5f) - (80.0f * 0.5f));

        if (!isPlaying) {
            // КНОПКА PLAY (Зеленая)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
            if (ImGui::Button("▶ PLAY", ImVec2(80, buttonSize))) {
                selectedEntity = entt::null;
                SaveSceneState(registry); // Делаем безопасный слепок!
                isPlaying = true;
            }
            ImGui::PopStyleColor(2);
        }
        else {
            // КНОПКА STOP (Красная)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
            if (ImGui::Button("■ STOP", ImVec2(80, buttonSize))) {
                selectedEntity = entt::null;
                RestoreSceneState(registry); // Возвращаем всё как было!
                isPlaying = false;
                render.isSceneDirty = true;
            }
            ImGui::PopStyleColor(2);
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }
    // Обычное число
    bool DrawPropertyFloat(const char* label, float& value, float resetValue = 0.0f, float speed = 0.1f, float min = 0.0f, float max = 0.0f) {
        bool changed = false;
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragFloat("##f", &value, speed, min, max)) changed = true;

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) { value = resetValue; changed = true; }
        ImGui::PopID();
        return changed;
    }

    // Выбор ассета (теперь показывает только ИМЯ файла, а не длинный путь!)
    bool DrawAssetPickerTable(const char* label, std::string& outPath, const std::vector<std::string>& extensions) {
        bool changed = false;
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);

        // ВОТ ЗДЕСЬ МАГИЯ: достаем только само имя файла для красоты
        std::string displayName = outPath.empty() ? "None" : fs::path(outPath).filename().string();

        // Рисуем кнопочку как в Unreal
        float buttonWidth = ImGui::GetContentRegionAvail().x - 28.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.11f, 0.14f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if (ImGui::Button(displayName.c_str(), ImVec2(buttonWidth, 0))) {
            ImGui::OpenPopup("AssetPickerPopup");
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(24.0f, 0))) ImGui::OpenPopup("AssetPickerPopup");

        if (ImGui::BeginPopup("AssetPickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Assets:");
            ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string(); bool match = false;
                    for (const auto& e : extensions) { if (ext == e) match = true; }
                    if (match) {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string();
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) { outPath = relPath; changed = true; }
                    }
                }
            }
            ImGui::EndPopup();
        }

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) { outPath = ""; changed = true; }
        ImGui::PopID();
        return changed;
    }
    void DeleteGameObject(entt::registry& registry, entt::entity target) {
        SaveState(registry);

        if (registry.all_of<HierarchyComponent>(target)) {
            entt::entity parent = registry.get<HierarchyComponent>(target).parent;
            if (parent != entt::null && registry.all_of<HierarchyComponent>(parent)) {
                auto& siblings = registry.get<HierarchyComponent>(parent).children;
                siblings.erase(std::remove(siblings.begin(), siblings.end(), target), siblings.end());
            }
        }
        DeleteEntityRecursive(registry, target);
    }

    bool IsDescendant(entt::registry& registry, entt::entity potentialChild, entt::entity potentialParent) {
        entt::entity curr = potentialChild;
        while (curr != entt::null && registry.all_of<HierarchyComponent>(curr)) {
            if (curr == potentialParent) return true;
            curr = registry.get<HierarchyComponent>(curr).parent;
        }
        return false;
    }

    // --- ПОСТ ПРОЦЕССИНГ ---
    struct PostProcessSettings {
        bool enableSSAO = true; float ssaoRadius = 0.5f; float ssaoBias = 0.025f; float ssaoIntensity = 2.0f; float ssaoPower = 2.0f;
        bool enableSSGI = true; int ssgiRayCount = 8; float ssgiStepSize = 0.4f; float ssgiThickness = 0.5f;
        int blurRange = 4; float gamma = 2.2f;
        bool autoExposure = true; float manualExposure = 1.0f; float exposureCompensation = 1.0f; float minBrightness = 0.5f; float maxBrightness = 3.0f;
        float contrast = 1.0f; float saturation = 1.0f;
        bool enableVignette = false; float vignetteIntensity = 0.5f;
        bool enableChromaticAberration = false; float caIntensity = 0.005f;
        bool enableBloom = true; float bloomThreshold = 1.0f; float bloomIntensity = 1.5f; int bloomBlurIterations = 10;
        bool enableLensFlares = true; float flareIntensity = 0.5f; float ghostDispersal = 0.3f; int ghosts = 4;
        float currentExposure = 1.0f; float temperature = 8000.0f;
        bool enableDoF = false; float focusDistance = 10.0f; float focusRange = 3.0f; float bokehSize = 2.0f;
        bool enableMotionBlur = false; float mbStrength = 0.5f;
        bool enableGodRays = false; float godRaysIntensity = 1.0f;
        bool enableFilmGrain = false; float grainIntensity = 0.05f;
        bool enableSharpen = false; float sharpenIntensity = 0.5f;
        bool enableFog = true; float fogDensity = 0.02f; float fogHeightFalloff = 0.2f; float fogBaseHeight = 0.0f;
        float fogColor[3] = { 0.5f, 0.6f, 0.7f };         float inscatterColor[3] = { 1.0f, 0.8f, 0.5f };         float inscatterPower = 8.0f;             float inscatterIntensity = 1.0f;
        bool enableContactShadows = true; float contactShadowLength = 0.05f; float contactShadowThickness = 0.1f; int contactShadowSteps = 16;
    } ppSettings;

    void SetupBurnhopeTheme() {
        ImGuiStyle& style = ImGui::GetStyle(); ImVec4* colors = style.Colors;
        style.WindowRounding = 6.0f; style.ChildRounding = 4.0f; style.FrameRounding = 4.0f; style.PopupRounding = 6.0f; style.TabRounding = 6.0f;
        style.WindowBorderSize = 1.0f; style.FrameBorderSize = 0.0f; style.PopupBorderSize = 1.0f; style.ItemSpacing = ImVec2(8, 6);

        // Теплый темный фон
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.11f, 0.10f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.09f, 0.08f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.11f, 0.10f, 0.98f);

        // Оранжевые акценты (кнопки, рамки, ползунки)
        colors[ImGuiCol_Border] = ImVec4(0.45f, 0.25f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.15f, 0.13f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.45f, 0.25f, 0.10f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.65f, 0.35f, 0.12f, 1.00f);

        colors[ImGuiCol_Button] = ImVec4(0.45f, 0.25f, 0.10f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.65f, 0.35f, 0.12f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.85f, 0.45f, 0.15f, 1.00f);

        colors[ImGuiCol_Header] = ImVec4(0.35f, 0.18f, 0.05f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.50f, 0.28f, 0.10f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.70f, 0.40f, 0.15f, 1.00f);

        colors[ImGuiCol_Tab] = ImVec4(0.22f, 0.14f, 0.08f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.50f, 0.28f, 0.10f, 1.00f);
        colors[ImGuiCol_TabActive] = ImVec4(0.45f, 0.25f, 0.10f, 1.00f);
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.12f, 0.11f, 0.10f, 1.00f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.18f, 0.15f, 0.13f, 1.00f);

        colors[ImGuiCol_CheckMark] = ImVec4(0.95f, 0.55f, 0.20f, 1.00f); // Яркая оранжевая галочка
        colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.45f, 0.15f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.95f, 0.55f, 0.20f, 1.00f);

        colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.11f, 0.10f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.15f, 0.13f, 1.00f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.09f, 0.08f, 1.00f);
    }

    UI(Window& window, const std::string& projectPath, const std::string& exePath) {
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuizmo::Enable(true);
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImFontConfig font_cfg; font_cfg.OversampleH = 2; font_cfg.OversampleV = 2;
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, &font_cfg, io.Fonts->GetGlyphRangesCyrillic());
        SetupBurnhopeTheme();
        ImGui_ImplGlfw_InitForOpenGL(window.window, true); ImGui_ImplOpenGL3_Init("#version 330");
        projectDirectory = projectPath; currentDirectory = projectPath; ExeDirectory = exePath;
        dirHistory.push_back(currentDirectory); dirHistoryIndex = 0;
        LoadPostProcessSettings();
    }
    // --- КАСТОМНЫЙ ЗАГОЛОВОК КОМПОНЕНТА С ИКОНКОЙ ---
    bool DrawComponentHeader(const char* title, const char* iconFileName, bool& removeClicked, bool isTransform = false) {
        // Используем стандартные флаги CollapsingHeader
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_CollapsingHeader;

        // МАГИЯ: добавляем 7 невидимых пробелов перед названием. 
        // Они освобождают идеальное место для нашей иконки!
        std::string label = std::string("       ") + title;

        bool isOpen = ImGui::CollapsingHeader(label.c_str(), flags);

        // Достаем координаты только что нарисованного прямоугольника заголовка
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();

        // Загружаем и рисуем нашу иконку поверх пустого места
        GLuint iconID = GetImageThumbnail(ExeDirectory.string() + "/Resources/" + iconFileName);
        if (iconID != 0) {
            float iconSize = 18.0f; // Можно сделать больше или меньше по вкусу
            // Треугольник ImGui обычно занимает ~20 пикселей. Ставим иконку сразу за ним (min.x + 24.0f)
            ImVec2 iconPos = ImVec2(min.x + 29.0f, min.y + (max.y - min.y - iconSize) * 0.5f);
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)iconID, iconPos, ImVec2(iconPos.x + iconSize, iconPos.y + iconSize));
        }

        // Рисуем крестик для удаления (но для Transform скрываем, его удалять нельзя)
        if (!isTransform) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 40);
            if (ImGui::Button((std::string("X##RM_") + title).c_str())) removeClicked = true;
        }

        return isOpen;
    }
    // --- МЕЛКИЕ ХЕЛПЕРЫ ДЛЯ ФАЙЛОВ ---
    std::string TruncateText(const std::string& text, float maxWidth) {
        if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
        std::string res = text;
        while (res.length() > 0 && ImGui::CalcTextSize((res + "...").c_str()).x > maxWidth) res.pop_back();
        return res + "...";
    }
    std::string GetFileTypeName(const std::string& ext, bool isDir) {
        if (isDir) return "Folder";
        if (ext == ".bhmat") return "Material";
        if (ext == ".bhtex") return "Texture";
        if (ext == ".bhscene") return "Scene";
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") return "Image";
        if (ext == ".bhtex") return "Texture";
        if (ext == ".obj" || ext == ".fbx") return "Model";
        return "File";
    }
    void MoveToRecycleBin(const std::string& path) {
        std::string winPath = fs::absolute(path).string();
        std::replace(winPath.begin(), winPath.end(), '/', '\\');
        winPath.push_back('\0'); winPath.push_back('\0');
        SHFILEOPSTRUCTA fileOp = { 0 }; fileOp.wFunc = FO_DELETE; fileOp.pFrom = winPath.c_str();
        fileOp.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
        SHFileOperationA(&fileOp);
    }
    bool IsSelected(const std::string& path) { return std::find(selectedAssets.begin(), selectedAssets.end(), path) != selectedAssets.end(); }
    std::string GetPrimarySelection() { return selectedAssets.empty() ? "" : selectedAssets.back(); }
    void NavigateTo(const fs::path& target) {
        if (currentDirectory == target) return;
        if (dirHistoryIndex < dirHistory.size() - 1) dirHistory.erase(dirHistory.begin() + dirHistoryIndex + 1, dirHistory.end());
        dirHistory.push_back(target); dirHistoryIndex++; currentDirectory = target;
        selectedAssets.clear(); renamingPath = ""; lastClickedIndex = -1;
    }
    void StartRename(const std::string& path) {
        renamingPath = path; strcpy_s(inlineRenameBuf, sizeof(inlineRenameBuf), fs::path(path).stem().string().c_str()); focusRename = true;
    }
    void ApplyRename() {
        if (!renamingPath.empty() && strlen(inlineRenameBuf) > 0) {
            fs::path oldP(renamingPath); std::string newName = std::string(inlineRenameBuf) + oldP.extension().string();
            fs::path newP = oldP.parent_path() / newName;
            if (oldP != newP && !fs::exists(newP)) {
                fs::rename(oldP, newP);
                auto it = std::find(selectedAssets.begin(), selectedAssets.end(), renamingPath);
                if (it != selectedAssets.end()) *it = newP.string();
            }
        }
        renamingPath = "";
    }
    void PasteCopiedItems(const fs::path& targetDir) {
        if (clipboardPaths.empty()) return;
        for (const auto& cbPath : clipboardPaths) {
            if (!fs::exists(cbPath)) continue;
            fs::path src(cbPath); fs::path dst = targetDir / src.filename();
            int copyCount = 1;
            while (fs::exists(dst)) { dst = targetDir / (src.stem().string() + " " + std::to_string(copyCount) + src.extension().string()); copyCount++; }
            if (isCut) fs::rename(src, dst); else fs::copy(src, dst, fs::copy_options::recursive);
        }
        if (isCut) { clipboardPaths.clear(); isCut = false; }
    }

    void LoadMaterialToProperties(const std::string& path) {
        memset(editAlbedo, 0, sizeof(editAlbedo)); memset(editNormal, 0, sizeof(editNormal));
        memset(editHeight, 0, sizeof(editHeight)); memset(editAO, 0, sizeof(editAO));
        memset(editMetallic, 0, sizeof(editMetallic)); memset(editRoughness, 0, sizeof(editRoughness));
        std::ifstream file(path);
        if (file.is_open()) {
            json j; try { file >> j; }
            catch (...) { return; }
            if (j.contains("textures")) {
                if (j["textures"].contains("albedo")) strcpy_s(editAlbedo, j["textures"]["albedo"].get<std::string>().c_str());
                if (j["textures"].contains("normal")) strcpy_s(editNormal, j["textures"]["normal"].get<std::string>().c_str());
                if (j["textures"].contains("height")) strcpy_s(editHeight, j["textures"]["height"].get<std::string>().c_str());
                if (j["textures"].contains("ao")) strcpy_s(editAO, j["textures"]["ao"].get<std::string>().c_str());
                if (j["textures"].contains("metallic")) strcpy_s(editMetallic, j["textures"]["metallic"].get<std::string>().c_str());
                if (j["textures"].contains("roughness")) strcpy_s(editRoughness, j["textures"]["roughness"].get<std::string>().c_str());
            }
        }
    }

    GLuint GetImageThumbnail(const std::string& fullPath) {
        if (imageThumbnails.find(fullPath) != imageThumbnails.end()) return imageThumbnails[fullPath];
        int w, h, c; unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &c, 4);
        if (!data) { imageThumbnails[fullPath] = 0; return 0; }
        GLuint id; glGenTextures(1, &id); glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(data); imageThumbnails[fullPath] = id; return id;
    }
    GLuint GetFileIcon(const std::string& ext, bool isDir) {
        if (isDir) return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_folder.png");
        if (ext == ".bhmat") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_material.png");
        if (ext == ".bhscene") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_scene.png");
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_model.png");
        return GetImageThumbnail(ExeDirectory.string() + "/Resources/icon_file.png");
    }

    // --- ОТРИСОВКА ОКНА: OUTLINER ---
    void DrawOutlinerNode(entt::registry& registry, entt::entity entity) {
        if (!registry.valid(entity)) return;
        auto& tag = registry.get<TagComponent>(entity);

        ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;
        if (selectedEntity == entity) nodeFlags |= ImGuiTreeNodeFlags_Selected;

        auto* hc = registry.try_get<HierarchyComponent>(entity);
        if (!hc || hc->children.empty()) nodeFlags |= ImGuiTreeNodeFlags_Leaf;

        bool nodeOpen = ImGui::TreeNodeEx((void*)(uintptr_t)entity, nodeFlags, tag.name.c_str());

        if (ImGui::IsItemClicked()) {
            selectedEntity = entity;
            if (registry.all_of<TransformComponent>(entity)) {
                auto& t = registry.get<TransformComponent>(entity).transform;
                ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(t.position), glm::value_ptr(t.rotation), glm::value_ptr(t.scale), glm::value_ptr(model));
            }
        }

        if (ImGui::BeginPopupContextItem()) {
            selectedEntity = entity;
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                SaveState(registry);
                entt::entity parent = hc ? hc->parent : entt::null;
                entt::entity newEnt = CloneHierarchy(registry, entity, parent);
                if (parent != entt::null) registry.get<HierarchyComponent>(parent).children.push_back(newEnt);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Create Child...")) {
                if (ImGui::MenuItem("Empty Object")) {
                    SaveState(registry);
                    entt::entity newE = registry.create();
                    registry.emplace<TagComponent>(newE, "Empty");
                    registry.emplace<TransformComponent>(newE);
                    registry.emplace<HierarchyComponent>(newE).parent = entity;
                    registry.get_or_emplace<HierarchyComponent>(entity).children.push_back(newE);
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete", "Del")) DeleteGameObject(registry, entity);
            ImGui::EndPopup();
        }

        float iconSize = 16.0f;
        float iconX = ImGui::GetWindowContentRegionMax().x - iconSize - 5.0f;
        auto DrawIcon = [&](const std::string& fileName) {
            GLuint texID = GetImageThumbnail(ExeDirectory.string() + "/Resources/" + fileName);
            if (texID != 0) { ImGui::SameLine(iconX); ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(iconSize, iconSize)); iconX -= (iconSize + 4.0f); }
            };

        if (registry.all_of<LightComponent>(entity)) DrawIcon("icon_light.png");
        if (registry.all_of<MeshComponent>(entity))  DrawIcon("icon_model.png");
        if (!registry.all_of<MeshComponent>(entity) && !registry.all_of<LightComponent>(entity)) DrawIcon("icon_folder.png");

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("OUTLINER_NODE", &entity, sizeof(entt::entity));
            ImGui::Text("Move %s", tag.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                if (dragged != entity && !IsDescendant(registry, entity, dragged)) {
                    SaveState(registry);
                    auto& draggedHc = registry.get_or_emplace<HierarchyComponent>(dragged);
                    if (draggedHc.parent != entt::null) {
                        auto& oldParentHc = registry.get<HierarchyComponent>(draggedHc.parent);
                        oldParentHc.children.erase(std::remove(oldParentHc.children.begin(), oldParentHc.children.end(), dragged), oldParentHc.children.end());
                    }
                    draggedHc.parent = entity;
                    registry.get_or_emplace<HierarchyComponent>(entity).children.push_back(dragged);
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (nodeOpen) {
            if (hc) {
                // Копия, чтобы безопасно итерироваться, если массив изменится
                auto children = hc->children;
                for (entt::entity child : children) DrawOutlinerNode(registry, child);
            }
            ImGui::TreePop();
        }
    }
    // --- НОВЫЕ ХЕЛПЕРЫ ДЛЯ СВЕТА И ФИЗИКИ ---

    // Выпадающий список (Combo)
    bool DrawPropertyCombo(const char* label, int& currentItem, const char* const items[], int itemsCount, int resetIndex = 0) {
        bool changed = false;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::Combo("##combo", &currentItem, items, itemsCount)) changed = true;

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) { currentItem = resetIndex; changed = true; }
        ImGui::PopID();
        return changed;
    }

    // Выбор цвета
    bool DrawPropertyColor(const char* label, glm::vec3& color, const glm::vec3& resetValue = glm::vec3(1.0f)) {
        bool changed = false;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::ColorEdit3("##color", glm::value_ptr(color))) changed = true;

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) { color = resetValue; changed = true; }
        ImGui::PopID();
        return changed;
    }

    // Галочка (Checkbox)
    bool DrawPropertyBool(const char* label, bool& value, bool resetValue = true) {
        bool changed = false;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);

        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(label);
        if (ImGui::Checkbox("##bool", &value)) changed = true;

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button("<", ImVec2(22, 22))) { value = resetValue; changed = true; }
        ImGui::PopID();
        return changed;
    }
    void DrawSceneOutliner(entt::registry& registry, ImGuiIO& io) {
        if (!showOutliner) return;
        ImGui::Begin("Scene Outliner", &showOutliner);

        if (ImGui::Button("+ Add", ImVec2(60, 25))) ImGui::OpenPopup("GlobalCreateMenu");
        ImGui::SameLine();
        if (ImGui::Button("Unparent", ImVec2(80, 25)) && selectedEntity != entt::null) {
            SaveState(registry);
            auto* hc = registry.try_get<HierarchyComponent>(selectedEntity);
            if (hc && hc->parent != entt::null) {
                auto& parentHc = registry.get<HierarchyComponent>(hc->parent);
                parentHc.children.erase(std::remove(parentHc.children.begin(), parentHc.children.end(), selectedEntity), parentHc.children.end());
                hc->parent = entt::null;
            }
        }

        if (ImGui::BeginPopup("GlobalCreateMenu")) {
            if (ImGui::MenuItem("Empty Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Empty"); registry.emplace<TransformComponent>(e); }
            if (ImGui::MenuItem("Mesh Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Mesh"); registry.emplace<TransformComponent>(e); registry.emplace<MeshComponent>(e); }
            if (ImGui::MenuItem("Light Source")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Light"); registry.emplace<TransformComponent>(e); registry.emplace<LightComponent>(e); }
            ImGui::EndPopup();
        }
        ImGui::Separator();

        ImGui::BeginChild("OutlinerList", ImVec2(0, -20));
        registry.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
            auto* hc = registry.try_get<HierarchyComponent>(entity);
            if (!hc || hc->parent == entt::null) {
                DrawOutlinerNode(registry, entity);
            }
            });

        if (ImGui::BeginPopupContextWindow("EmptySpaceMenu", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::BeginMenu("Create...")) {
                if (ImGui::MenuItem("Empty Object")) { SaveState(registry); entt::entity e = registry.create(); registry.emplace<TagComponent>(e, "Empty"); registry.emplace<TransformComponent>(e); }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OUTLINER_NODE")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                SaveState(registry);
                auto* hc = registry.try_get<HierarchyComponent>(dragged);
                if (hc && hc->parent != entt::null) {
                    auto& parentHc = registry.get<HierarchyComponent>(hc->parent);
                    parentHc.children.erase(std::remove(parentHc.children.begin(), parentHc.children.end(), dragged), parentHc.children.end());
                    hc->parent = entt::null;
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
        ImGui::End();
    }

  

    void DrawSceneInspector(entt::registry& registry, Render& render) {
        if (!showInspector) return;
        ImGui::Begin("Scene Inspector", &showInspector);

        if (selectedEntity == entt::null || !registry.valid(selectedEntity)) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an object in the scene"); ImGui::End(); return;
        }

        auto& tag = registry.get<TagComponent>(selectedEntity);
        char nameBuf[128]; strcpy_s(nameBuf, sizeof(nameBuf), tag.name.c_str());
        ImGui::PushItemWidth(-1);
        if (ImGui::InputText("##ObjectName", nameBuf, sizeof(nameBuf))) { tag.name = nameBuf; }
        ImGui::PopItemWidth(); ImGui::Spacing();

        // --- ТРАНСФОРМ ---
        if (registry.all_of<TransformComponent>(selectedEntity)) {
            auto& tComp = registry.get<TransformComponent>(selectedEntity);
            bool dummyRemove = false;
            if (DrawComponentHeader("Transform", "icon_transform.png", dummyRemove, true)) {

                if (BeginInspectorTable("TransformTable")) {
                    bool transformChanged = false;

                    if (DrawPropertyVec3("Position", tComp.transform.position, glm::vec3(0.0f))) transformChanged = true;
                    if (ImGui::IsItemActivated()) SaveState(registry);

                    if (DrawPropertyVec3("Rotation", tComp.transform.rotation, glm::vec3(0.0f))) transformChanged = true;
                    if (ImGui::IsItemActivated()) SaveState(registry);

                    if (DrawPropertyVec3("Scale", tComp.transform.scale, glm::vec3(1.0f))) transformChanged = true;
                    if (ImGui::IsItemActivated()) SaveState(registry);

                    if (transformChanged) {
                        tComp.transform.updatematrix = true;
                        if (registry.all_of<PhysicsComponent>(selectedEntity)) registry.get<PhysicsComponent>(selectedEntity).updatePhysicsTransform = true;
                        ImGuizmo::RecomposeMatrixFromComponents(glm::value_ptr(tComp.transform.position), glm::value_ptr(tComp.transform.rotation), glm::value_ptr(tComp.transform.scale), glm::value_ptr(model));
                        isSceneUnsaved = true;
                        render.isSceneDirty = true;
                    }
                    ImGui::EndTable();
                }
            }
        }

        // --- MESH RENDERER ---
        // --- MESH RENDERER ---
        if (registry.all_of<MeshComponent>(selectedEntity)) {
            auto& meshComp = registry.get<MeshComponent>(selectedEntity);
            bool removeMesh = false;
            if (DrawComponentHeader("Mesh", "icon_mesh.png", removeMesh)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_MESH")) removeMesh = true;

                // Начинаем табличку сразу, чтобы галочки тоже были ровными
                if (BeginInspectorTable("MeshTable")) {
                    bool meshChanged = false;

                    // Раздел: Основные настройки
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Settings"); // Оранжевый заголовок

                    if (DrawPropertyBool("Static", meshComp.isStatic, false)) meshChanged = true;
                    if (DrawPropertyBool("Visible", meshComp.isVisible, true)) meshChanged = true;
                    if (DrawPropertyBool("Cast Shadow", meshComp.castShadow, true)) meshChanged = true;

                    if (meshChanged) {
                        SaveState(registry);
                        render.isSceneDirty = true;
                        isSceneUnsaved = true;
                        render.isSceneDirty = true;
                    }

                    // Раздел: Геометрия
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Geometry");

                    // Выбор модели!
                    if (DrawAssetPickerTable("Model", meshComp.modelPath, { ".bhmesh" })) {
                        SaveState(registry);
                        render.isSceneDirty = true;
                        fs::path absProjectDir = fs::absolute(projectDirectory);
                        std::string fullModelPath = (absProjectDir / meshComp.modelPath).string();

                        if (Serializer::loadedModels.find(fullModelPath) == Serializer::loadedModels.end()) {
                            Serializer::loadedModels[fullModelPath] = new Model(fullModelPath, projectDirectory.string());
                        }
                        Model* newModel = Serializer::loadedModels[fullModelPath];

                        if (newModel && !newModel->meshes.empty()) {
                            meshComp.materialPaths.clear();
                            for (const std::string& rawMatPath : newModel->loadedMaterialPaths) {
                                if (rawMatPath.empty()) meshComp.materialPaths.push_back("");
                                else {
                                    fs::path p(rawMatPath);
                                    if (p.is_absolute()) meshComp.materialPaths.push_back(fs::relative(p, absProjectDir).generic_string());
                                    else meshComp.materialPaths.push_back(p.generic_string());
                                }
                            }

                            meshComp.renderer.subMeshes.clear();
                            for (int i = 0; i < newModel->meshes.size(); i++) {
                                std::string relMatPath = (i < meshComp.materialPaths.size()) ? meshComp.materialPaths[i] : "";
                                Material* mat = nullptr;
                                if (!relMatPath.empty()) {
                                    std::string fullMatPath = (absProjectDir / relMatPath).string();
                                    mat = Serializer::LoadMaterial(fullMatPath, projectDirectory.string());
                                }
                                if (mat == nullptr) mat = new Material();
                                meshComp.renderer.AddSubMesh(&newModel->meshes[i], mat);
                            }
                        }
                        if (registry.all_of<PhysicsComponent>(selectedEntity)) registry.get<PhysicsComponent>(selectedEntity).rebuildPhysics = true;
                    }

                    // Раздел: Отрисовка материалов
                    if (!meshComp.renderer.subMeshes.empty()) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Materials");

                        if (meshComp.materialPaths.size() != meshComp.renderer.subMeshes.size()) meshComp.materialPaths.resize(meshComp.renderer.subMeshes.size(), "");

                        for (int i = 0; i < meshComp.renderer.subMeshes.size(); i++) {
                            std::string mName = meshComp.renderer.subMeshes[i].mesh ? meshComp.renderer.subMeshes[i].mesh->name : "Mesh";
                            char slotName[512]; sprintf_s(slotName, "Slot [%d]", i);

                            if (DrawAssetPickerTable(slotName, meshComp.materialPaths[i], { ".bhmat" })) {
                                SaveState(registry);
                                render.isSceneDirty = true;
                                std::string fullMatPath = (projectDirectory / meshComp.materialPaths[i]).string();
                                Material* newMat = Serializer::LoadMaterial(fullMatPath, projectDirectory.string());
                                if (newMat) meshComp.renderer.subMeshes[i].material = newMat;
                            }
                        }
                    }
                    ImGui::EndTable();
                }
            }
            if (removeMesh) { SaveState(registry); registry.erase<MeshComponent>(selectedEntity); render.isSceneDirty = true; }
        }

        // --- LIGHT COMPONENT ---
        if (registry.all_of<LightComponent>(selectedEntity)) {
            auto& lComp = registry.get<LightComponent>(selectedEntity).light;
            bool removeLight = false;
            if (DrawComponentHeader("Light", "icon_light.png", removeLight)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_LIGHT")) removeLight = true;

                if (BeginInspectorTable("LightTable")) {
                    bool stateChanged = false;

                    // Основные настройки
                    if (DrawPropertyBool("Enable Light", lComp.enable, true)) stateChanged = true;
                    if (DrawPropertyBool("Cast Shadows", lComp.castShadows, true)) stateChanged = true;

                    const char* lightTypes[] = { "Directional", "Point", "Spot", "Rect", "Sky" };
                    int currentType = (int)lComp.type;
                    if (DrawPropertyCombo("Type", currentType, lightTypes, IM_ARRAYSIZE(lightTypes), 0)) {
                        lComp.type = (LightType)currentType; stateChanged = true;
                    }

                    const char* mobilityTypes[] = { "Static", "Movable" };
                    int currentMob = (int)lComp.mobility;
                    if (DrawPropertyCombo("Mobility", currentMob, mobilityTypes, IM_ARRAYSIZE(mobilityTypes), 1)) {
                        lComp.mobility = (LightMobility)currentMob; stateChanged = true;
                    }

                    // Визуал Света
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Appearance");

                    if (DrawPropertyColor("Color", lComp.color, glm::vec3(1.0f))) stateChanged = true;
                    if (DrawPropertyFloat("Intensity", lComp.intensity, 1.0f, 0.1f, 0.0f, 1000.0f)) stateChanged = true;

                    // Специфичные параметры в зависимости от типа света
                    if (lComp.type == LightType::Point || lComp.type == LightType::Spot) {
                        ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Shape");
                        if (DrawPropertyFloat("Radius", lComp.radius, 10.0f, 0.5f, 0.1f, 500.0f)) stateChanged = true;
                    }

                    if (lComp.type == LightType::Spot) {
                        if (DrawPropertyFloat("Inner Angle", lComp.innerCone, 15.0f, 0.5f, 0.0f, lComp.outerCone)) stateChanged = true;
                        if (DrawPropertyFloat("Outer Angle", lComp.outerCone, 30.0f, 0.5f, lComp.innerCone, 90.0f)) stateChanged = true;
                    }

                    if (stateChanged) { SaveState(registry); isSceneUnsaved = true;
                    render.isSceneDirty = true;
                    }
                    ImGui::EndTable();
                }
            }
            if (removeLight) { SaveState(registry); registry.erase<LightComponent>(selectedEntity); }
        }

        // --- PHYSICS COMPONENT ---
        if (registry.all_of<PhysicsComponent>(selectedEntity)) {
            auto& phys = registry.get<PhysicsComponent>(selectedEntity);
            bool removePhysics = false;

            if (DrawComponentHeader("Physics", "icon_physics.png", removePhysics)) {
                ImGui::SameLine(ImGui::GetWindowWidth() - 40);
                if (ImGui::Button("X##RM_PHYSICS")) removePhysics = true;

                if (BeginInspectorTable("PhysicsTable")) {
                    bool rebuild = false;

                    // Настройки твердого тела
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Rigid Body");

                    const char* rbTypes[] = { "Static", "Dynamic" };
                    int currentRbType = (int)phys.bodyType;
                    if (DrawPropertyCombo("Body Type", currentRbType, rbTypes, IM_ARRAYSIZE(rbTypes), 1)) {
                        phys.bodyType = (RigidBodyType)currentRbType; rebuild = true;
                    }

                    // Масса и трение
                    if (DrawPropertyFloat("Mass", phys.mass, 10.0f, 0.1f, 0.0f, 1000.0f)) rebuild = true;
                    if (DrawPropertyFloat("Friction", phys.friction, 0.5f, 0.01f, 0.0f, 1.0f)) rebuild = true;
                    if (DrawPropertyFloat("Restitution", phys.restitution, 0.5f, 0.01f, 0.0f, 1.0f)) rebuild = true;

                    // Настройки коллайдера
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.2f, 1.0f), "Collider");

                    const char* colTypes[] = { "Box", "Sphere", "Plane" };
                    int currentColType = (int)phys.colliderType;
                    if (DrawPropertyCombo("Shape", currentColType, colTypes, IM_ARRAYSIZE(colTypes), 0)) {
                        phys.colliderType = (ColliderType)currentColType; rebuild = true;
                    }

                    // Форма коллайдера (отображается динамически)
                    if (phys.colliderType == ColliderType::Box) {
                        if (DrawPropertyVec3("Extents", phys.extents, glm::vec3(1.0f))) rebuild = true;
                    }
                    else if (phys.colliderType == ColliderType::Sphere) {
                        if (DrawPropertyFloat("Radius", phys.radius, 1.0f, 0.1f, 0.0f, 100.0f)) rebuild = true;
                    }

                    if (rebuild) { SaveState(registry); phys.rebuildPhysics = true;  isSceneUnsaved = true; isSceneUnsaved = true; isSceneUnsaved = true;
                    }
                    ImGui::EndTable();
                }
            }
            if (removePhysics) { SaveState(registry); registry.erase<PhysicsComponent>(selectedEntity); }
        }

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("Add Component", ImVec2(-1, 30))) ImGui::OpenPopup("AddComponentPopup");
        if (ImGui::BeginPopup("AddComponentPopup")) {
            if (!registry.all_of<MeshComponent>(selectedEntity) && ImGui::MenuItem("Mesh Renderer")) { SaveState(registry); registry.emplace<MeshComponent>(selectedEntity); }
            if (!registry.all_of<LightComponent>(selectedEntity) && ImGui::MenuItem("Light Component")) { SaveState(registry); registry.emplace<LightComponent>(selectedEntity); }
            if (!registry.all_of<PhysicsComponent>(selectedEntity) && ImGui::MenuItem("Physics Component")) { SaveState(registry); auto& p = registry.emplace<PhysicsComponent>(selectedEntity); p.rebuildPhysics = true; }
            ImGui::EndPopup();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::End();
    }
        void DrawFolderTree(const fs::path& dir) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (currentDirectory == dir) flags |= ImGuiTreeNodeFlags_Selected;
        bool isLeaf = true;
        for (auto& entry : fs::directory_iterator(dir)) { if (entry.is_directory()) { isLeaf = false; break; } }
        if (isLeaf) flags |= ImGuiTreeNodeFlags_Leaf;
        std::string nodeName = dir == projectDirectory ? "All (Project)" : dir.filename().string();
        bool isOpen = ImGui::TreeNodeEx(nodeName.c_str(), flags);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) NavigateTo(dir);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                for (const auto& selPath : selectedAssets) {
                    fs::path src(selPath);
                    if (src != dir) fs::rename(src, dir / src.filename());
                }
                selectedAssets.clear();
            }
            ImGui::EndDragDropTarget();
        }
        if (isOpen) {
            for (auto& entry : fs::directory_iterator(dir)) { if (entry.is_directory()) DrawFolderTree(entry.path()); }
            ImGui::TreePop();
        }
    }
    GLuint GetMaterialThumbnail(const std::string& matPath) {
        std::ifstream file(matPath); if (!file.is_open()) return 0;
        json j; try { file >> j; }
        catch (...) { return 0; }
        if (!j.contains("textures") || !j["textures"].contains("albedo")) return 0;
        return GetImageThumbnail(projectDirectory.string() + "/" + j["textures"]["albedo"].get<std::string>());
    }
  
                void DrawContentBrowser() {
        if (!showContentBrowser) return;
        ImGui::Begin("Content Browser", &showContentBrowser);
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !selectedAssets.empty() && renamingPath.empty()) {
                for (const auto& path : selectedAssets) MoveToRecycleBin(path);
                selectedAssets.clear(); lastClickedIndex = -1;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F2) && selectedAssets.size() == 1 && renamingPath.empty()) {
                StartRename(selectedAssets[0]);
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !selectedAssets.empty()) { clipboardPaths = selectedAssets; isCut = false; }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X) && !selectedAssets.empty()) { clipboardPaths = selectedAssets; isCut = true; }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clipboardPaths.empty()) { PasteCopiedItems(currentDirectory); }
        }
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (dirHistoryIndex > 0) { if (ImGui::Button("<")) { dirHistoryIndex--; currentDirectory = dirHistory[dirHistoryIndex]; selectedAssets.clear(); } }
        else { ImGui::TextDisabled("<"); }
        ImGui::SameLine();
        if (dirHistoryIndex < dirHistory.size() - 1) { if (ImGui::Button(">")) { dirHistoryIndex++; currentDirectory = dirHistory[dirHistoryIndex]; selectedAssets.clear(); } }
        else { ImGui::TextDisabled(">"); }
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
        ImGui::PopStyleColor();
        if (ImGui::Button(" + Create ")) ImGui::OpenPopup("CreateMenuPopup");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##Search", "Search all folders...", searchBuffer, sizeof(searchBuffer));
        ImGui::SameLine(); ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("All")) NavigateTo(projectDirectory);
        if (ImGui::BeginDragDropTarget()) {             if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                for (const auto& selPath : selectedAssets) { fs::path src(selPath); if (src != projectDirectory) fs::rename(src, projectDirectory / src.filename()); }
                selectedAssets.clear();
            }
            ImGui::EndDragDropTarget();
        }
        fs::path rel = fs::relative(currentDirectory, projectDirectory); fs::path accum = projectDirectory;
        if (rel.string() != ".") {
            for (auto it = rel.begin(); it != rel.end(); ++it) {
                ImGui::SameLine(); ImGui::Text(">"); ImGui::SameLine(); accum /= *it;
                if (ImGui::Button(it->string().c_str())) NavigateTo(accum);
                                if (ImGui::BeginDragDropTarget()) {
                    if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                        for (const auto& selPath : selectedAssets) { fs::path src(selPath); if (src != accum) fs::rename(src, accum / src.filename()); }
                        selectedAssets.clear();
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }
        ImGui::PopStyleColor();
        ImGui::Separator();
                        if (ImGui::BeginPopup("CreateMenuPopup")) {
            if (ImGui::MenuItem("New Folder")) {
                fs::path newPath = currentDirectory / "New Folder"; int count = 1;
                while (fs::exists(newPath)) { newPath = currentDirectory / ("New Folder " + std::to_string(count)); count++; }
                fs::create_directory(newPath);
                selectedAssets.clear(); selectedAssets.push_back(newPath.string()); StartRename(newPath.string());
            }
            if (ImGui::MenuItem("Material (.bhmat)")) {
                fs::path newPath = currentDirectory / "New Material.bhmat"; int count = 1;
                while (fs::exists(newPath)) { newPath = currentDirectory / ("New Material " + std::to_string(count) + ".bhmat"); count++; }
                json j; j["name"] = "New Material"; j["textures"] = { {"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""} };
                std::ofstream file(newPath); file << j.dump(4);
                selectedAssets.clear(); selectedAssets.push_back(newPath.string()); LoadMaterialToProperties(newPath.string()); StartRename(newPath.string());
            }
            ImGui::EndPopup();
        }
        ImGui::Columns(2, "CB_Columns", true);
        if (ImGui::GetColumnWidth() == ImGui::GetContentRegionAvail().x) ImGui::SetColumnWidth(0, 200.0f);
        ImGui::SetColumnWidth(0, 200.0f);
                ImGui::BeginChild("LeftTreePanel");
        DrawFolderTree(projectDirectory);
        ImGui::EndChild();
        ImGui::NextColumn();
                ImGui::BeginChild("RightGridPanel");
  
                std::vector<fs::directory_entry> items;
                
        std::string searchStr(searchBuffer); std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(), ::tolower);
        if (!searchStr.empty()) {
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                std::string lowerName = entry.path().filename().string(); std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                if (lowerName.find(searchStr) != std::string::npos) items.push_back(entry);
            }
        }
        else {
            for (auto& entry : fs::directory_iterator(currentDirectory)) {
                items.push_back(entry);

                // --- АВТО-КОНВЕРТАЦИЯ ПРИ ОБНАРУЖЕНИИ ---
                // --- АВТО-КОНВЕРТАЦИЯ ПРИ ОБНАРУЖЕНИИ ---
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".fbx" || ext == ".obj") {
                        fs::path compressedPath = entry.path();
                        compressedPath.replace_extension(".bhmesh");

                        if (!fs::exists(compressedPath)) {
                            std::cout << "Detected new model, compiling: " << entry.path().filename() << "...\n";

                            if (ModelImporter::ImportModel(entry.path().string(), compressedPath.string(), projectDirectory.string())) {
                                try {
                                    fs::remove(entry.path());
                                    std::cout << "Deleted original model: " << entry.path().filename() << "\n";
                                }
                                catch (...) {}
                                continue;
                            }
                        }
                    }
                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                        fs::path compressedPath = entry.path();
                        compressedPath.replace_extension(".bhtex");
                        if (!fs::exists(compressedPath)) {
                            std::cout << "Detected new texture, compiling: " << entry.path().filename() << "...\n";

                            // Определяем тип текстуры по имени файла
                            std::string name = entry.path().stem().string();
                            // Приводим к нижнему регистру для надёжного поиска
                            std::string nameLower = name;
                            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

                            BHTexType texType = BHTexType::Color; // по умолчанию — цвет

                            if (nameLower.find("normal") != std::string::npos ||
                                nameLower.find("nrm") != std::string::npos ||
                                nameLower.find("_nor") != std::string::npos ||
                                nameLower.find("_n.") != std::string::npos) {
                                texType = BHTexType::Normal;
                                std::cout << "  -> Type: Normal (BC5)\n";
                            }
                            else if (nameLower.find("rough") != std::string::npos ||
                                nameLower.find("metal") != std::string::npos ||
                                nameLower.find("ao") != std::string::npos ||
                                nameLower.find("height") != std::string::npos ||
                                nameLower.find("occlu") != std::string::npos ||
                                nameLower.find("_r.") != std::string::npos ||
                                nameLower.find("_m.") != std::string::npos) {
                                texType = BHTexType::Linear;
                                std::cout << "  -> Type: Linear (DXT1 no sRGB)\n";
                            }
                            else {
                                std::cout << "  -> Type: Color (DXT5 sRGB)\n";
                            }

                            if (TextureImporter::ImportTexture(entry.path().string(),
                                compressedPath.string(),
                                texType)) {
                                try {
                                    fs::remove(entry.path());
                                    std::cout << "Deleted original file: " << entry.path().filename() << "\n";
                                }
                                catch (const fs::filesystem_error& e) {
                                    std::cerr << "Cannot delete file: " << e.what() << "\n";
                                }
                                continue;
                            }
                        }
                    }
                }
            }
        }
        std::sort(items.begin(), items.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            if (a.is_directory() && !b.is_directory()) return true;
            if (!a.is_directory() && b.is_directory()) return false;
            return a.path().filename().string() < b.path().filename().string();
            });
        if (ImGui::BeginPopupContextWindow("CB_Bg_Context", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New Folder")) {
                fs::path p = currentDirectory / "New Folder"; int c = 1; while (fs::exists(p)) { p = currentDirectory / ("New Folder " + std::to_string(c++)); }
                fs::create_directory(p); selectedAssets.clear(); selectedAssets.push_back(p.string()); StartRename(p.string());
            }
            if (ImGui::MenuItem("Material (.bhmat)")) {
                fs::path p = currentDirectory / "New Material.bhmat"; int c = 1; while (fs::exists(p)) { p = currentDirectory / ("New Material " + std::to_string(c++) + ".bhmat"); }
                json j; j["name"] = "New Material"; j["textures"] = { {"albedo", ""}, {"normal", ""}, {"height", ""}, {"ao", ""}, {"metallic", ""}, {"roughness", ""} };
                std::ofstream file(p); file << j.dump(4); selectedAssets.clear(); selectedAssets.push_back(p.string()); LoadMaterialToProperties(p.string()); StartRename(p.string());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !clipboardPaths.empty())) { PasteCopiedItems(currentDirectory); }
            ImGui::EndPopup();
        }
                float padding = 16.0f;
        float thumbnailSize = 64.0f;
                float itemWidth = thumbnailSize + 16.0f;
        float itemHeight = thumbnailSize + 45.0f;
        float cellSize = itemWidth + padding;
        float panelWidth = ImGui::GetContentRegionAvail().x;
        int columnCount = (int)(panelWidth / cellSize); if (columnCount < 1) columnCount = 1;
        ImGui::Columns(columnCount, 0, false);
        for (int i = 0; i < items.size(); ++i) {
            auto& entry = items[i];
            const auto& path = entry.path(); std::string pathStr = path.string(); std::string ext = path.extension().string();
            bool isDir = entry.is_directory();
            std::string nameNoExt = path.stem().string();
            std::string typeStr = GetFileTypeName(ext, isDir);
            ImGui::PushID(pathStr.c_str());
                        float colWidth = ImGui::GetColumnWidth();
            float offsetX = (colWidth - itemWidth) / 2.0f;
            if (offsetX < 0) offsetX = 0;             ImVec2 startPos = ImGui::GetCursorScreenPos();
                        ImVec2 itemPos = ImVec2(startPos.x + offsetX, startPos.y + padding / 2.0f);
            bool isSel = IsSelected(pathStr);
            bool isHovered = ImGui::IsMouseHoveringRect(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight));
                        if (isSel) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(36, 112, 204, 150), 8.0f);
            else if (isHovered) ImGui::GetWindowDrawList()->AddRectFilled(itemPos, ImVec2(itemPos.x + itemWidth, itemPos.y + itemHeight), IM_COL32(60, 70, 85, 120), 8.0f);
                        ImGui::SetCursorScreenPos(itemPos);
            ImGui::InvisibleButton("##hitbox", ImVec2(itemWidth, itemHeight));
                        if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseClicked(0)) {
                    if (ImGui::GetIO().KeyCtrl) {
                        if (isSel) selectedAssets.erase(std::remove(selectedAssets.begin(), selectedAssets.end(), pathStr), selectedAssets.end());
                        else { selectedAssets.push_back(pathStr); if (ext == ".bhmat") LoadMaterialToProperties(pathStr); }
                        lastClickedIndex = i;
                    }
                    else if (ImGui::GetIO().KeyShift && lastClickedIndex != -1) {
                        selectedAssets.clear();
                        int start = std::min(i, lastClickedIndex); int end = std::max(i, lastClickedIndex);
                        for (int j = start; j <= end; j++) { selectedAssets.push_back(items[j].path().string()); }
                    }
                    else {
                        if (!isSel) {
                            selectedAssets.clear(); selectedAssets.push_back(pathStr);
                            if (ext == ".bhmat") LoadMaterialToProperties(pathStr);
                        }
                        lastClickedIndex = i;
                    }
                }
                if (ImGui::IsMouseReleased(0) && !ImGui::IsMouseDragging(0)) {
                    if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && isSel && selectedAssets.size() > 1) {
                        selectedAssets.clear(); selectedAssets.push_back(pathStr);
                        if (ext == ".bhmat") LoadMaterialToProperties(pathStr);
                    }
                }
                if (ImGui::IsMouseDoubleClicked(0)) {
                    if (isDir) NavigateTo(path);
                }
            }
                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                if (!isSel) { selectedAssets.clear(); selectedAssets.push_back(pathStr); }
                ImGui::SetDragDropPayload("CB_ITEMS", nullptr, 0);
                ImGui::Text("Move %d items", (int)selectedAssets.size()); ImGui::EndDragDropSource();
            }
            if (isDir && ImGui::BeginDragDropTarget()) {
                if (ImGui::AcceptDragDropPayload("CB_ITEMS")) {
                    for (const auto& selPath : selectedAssets) {
                        fs::path src(selPath); if (src != path) fs::rename(src, path / src.filename());
                    }
                    selectedAssets.clear();
                }
                ImGui::EndDragDropTarget();
            }
                        if (ImGui::BeginPopupContextItem("ItemContext")) {
                if (!isSel) { selectedAssets.clear(); selectedAssets.push_back(pathStr); if (ext == ".bhmat") LoadMaterialToProperties(pathStr); }
                if (ImGui::MenuItem("Open")) { if (isDir) NavigateTo(path); } ImGui::Separator();
                if (ImGui::MenuItem("Cut", "Ctrl+X")) { clipboardPaths = selectedAssets; isCut = true; }
                if (ImGui::MenuItem("Copy", "Ctrl+C")) { clipboardPaths = selectedAssets; isCut = false; } ImGui::Separator();
                if (ImGui::MenuItem("Rename", "F2", false, selectedAssets.size() == 1)) { StartRename(pathStr); }
                if (ImGui::MenuItem("Delete", "Del")) { for (auto& p : selectedAssets) MoveToRecycleBin(p); selectedAssets.clear(); }
                ImGui::EndPopup();
            }
                        ImGui::SetCursorScreenPos(ImVec2(itemPos.x + (itemWidth - thumbnailSize) / 2.0f, itemPos.y + 6.0f));
            GLuint texID = 0;
            if (!isDir && (ext == ".png" || ext == ".jpg" || ext == ".jpeg")) {
                texID = GetImageThumbnail(pathStr);             }
            else {
                texID = GetFileIcon(ext, isDir);             }
            if (texID != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(thumbnailSize, thumbnailSize));
            }
            else {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.22f, 0.25f, 1.0f));
                ImGui::Button(isDir ? "DIR" : "FILE", ImVec2(thumbnailSize, thumbnailSize));
                ImGui::PopStyleColor();
            }
                        if (renamingPath == pathStr) {
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + 4, itemPos.y + thumbnailSize + 10.0f));
                ImGui::SetNextItemWidth(itemWidth - 8);
                if (focusRename) { ImGui::SetKeyboardFocusHere(); focusRename = false; ImGui::SetScrollHereY(); }
                if (ImGui::InputText("##rename", inlineRenameBuf, sizeof(inlineRenameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) { ApplyRename(); }
                if (!ImGui::IsItemActive() && !focusRename && ImGui::IsMouseClicked(0) && !ImGui::IsItemHovered()) { ApplyRename(); }
            }
            else {
                                std::string truncName = TruncateText(nameNoExt, itemWidth - 8.0f);
                float textOffset = (itemWidth - ImGui::CalcTextSize(truncName.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + textOffset, itemPos.y + thumbnailSize + 10.0f));
                ImGui::Text("%s", truncName.c_str());
                                float typeOffset = (itemWidth - ImGui::CalcTextSize(typeStr.c_str()).x) / 2.0f;
                ImGui::SetCursorScreenPos(ImVec2(itemPos.x + typeOffset, itemPos.y + thumbnailSize + 26.0f));
                ImGui::TextColored(ImVec4(0.5f, 0.55f, 0.6f, 1.0f), "%s", typeStr.c_str());
            }
            ImGui::NextColumn(); ImGui::PopID();
        }
        ImGui::Columns(1); ImGui::EndChild(); ImGui::Columns(1);
        ImGui::End();
    }
            void SavePostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json";
        json j;
                j["enableSSAO"] = ppSettings.enableSSAO; j["ssaoRadius"] = ppSettings.ssaoRadius; j["ssaoBias"] = ppSettings.ssaoBias; j["ssaoIntensity"] = ppSettings.ssaoIntensity; j["ssaoPower"] = ppSettings.ssaoPower;
        j["enableSSGI"] = ppSettings.enableSSGI; j["ssgiRayCount"] = ppSettings.ssgiRayCount; j["ssgiStepSize"] = ppSettings.ssgiStepSize; j["ssgiThickness"] = ppSettings.ssgiThickness; j["blurRange"] = ppSettings.blurRange;
                j["autoExposure"] = ppSettings.autoExposure; j["manualExposure"] = ppSettings.manualExposure; j["exposureCompensation"] = ppSettings.exposureCompensation; j["minBrightness"] = ppSettings.minBrightness; j["maxBrightness"] = ppSettings.maxBrightness;
        j["contrast"] = ppSettings.contrast; j["saturation"] = ppSettings.saturation; j["temperature"] = ppSettings.temperature; j["gamma"] = ppSettings.gamma;
                j["enableVignette"] = ppSettings.enableVignette; j["vignetteIntensity"] = ppSettings.vignetteIntensity;
        j["enableChromaticAberration"] = ppSettings.enableChromaticAberration; j["caIntensity"] = ppSettings.caIntensity;
        j["enableBloom"] = ppSettings.enableBloom; j["bloomThreshold"] = ppSettings.bloomThreshold; j["bloomIntensity"] = ppSettings.bloomIntensity; j["bloomBlurIterations"] = ppSettings.bloomBlurIterations;
        j["enableLensFlares"] = ppSettings.enableLensFlares; j["flareIntensity"] = ppSettings.flareIntensity; j["ghostDispersal"] = ppSettings.ghostDispersal; j["ghosts"] = ppSettings.ghosts;
        j["enableGodRays"] = ppSettings.enableGodRays; j["godRaysIntensity"] = ppSettings.godRaysIntensity;
        j["enableFilmGrain"] = ppSettings.enableFilmGrain; j["grainIntensity"] = ppSettings.grainIntensity;
        j["enableSharpen"] = ppSettings.enableSharpen; j["sharpenIntensity"] = ppSettings.sharpenIntensity;
                j["enableDoF"] = ppSettings.enableDoF; j["focusDistance"] = ppSettings.focusDistance; j["focusRange"] = ppSettings.focusRange; j["bokehSize"] = ppSettings.bokehSize;
        j["enableMotionBlur"] = ppSettings.enableMotionBlur; j["mbStrength"] = ppSettings.mbStrength;
        j["enableFog"] = ppSettings.enableFog; j["fogDensity"] = ppSettings.fogDensity; j["fogHeightFalloff"] = ppSettings.fogHeightFalloff; j["fogBaseHeight"] = ppSettings.fogBaseHeight;
        j["inscatterPower"] = ppSettings.inscatterPower; j["inscatterIntensity"] = ppSettings.inscatterIntensity;
                j["fogColor"] = { ppSettings.fogColor[0], ppSettings.fogColor[1], ppSettings.fogColor[2] };
        j["inscatterColor"] = { ppSettings.inscatterColor[0], ppSettings.inscatterColor[1], ppSettings.inscatterColor[2] };
        std::ofstream file(path);
        file << j.dump(4);
    }
    void LoadPostProcessSettings() {
        std::string path = projectDirectory.string() + "/postprocess.json";
        std::ifstream file(path);
        if (!file.is_open()) { SavePostProcessSettings(); return; }
        json j; try { file >> j; }
        catch (...) { return; }
                auto loadFloat = [&](const char* key, float& val) { if (j.contains(key)) val = j[key]; };
        auto loadInt = [&](const char* key, int& val) { if (j.contains(key)) val = j[key]; };
        auto loadBool = [&](const char* key, bool& val) { if (j.contains(key)) val = j[key]; };
        loadBool("enableSSAO", ppSettings.enableSSAO); loadFloat("ssaoRadius", ppSettings.ssaoRadius); loadFloat("ssaoBias", ppSettings.ssaoBias); loadFloat("ssaoIntensity", ppSettings.ssaoIntensity); loadFloat("ssaoPower", ppSettings.ssaoPower);
        loadBool("enableSSGI", ppSettings.enableSSGI); loadInt("ssgiRayCount", ppSettings.ssgiRayCount); loadFloat("ssgiStepSize", ppSettings.ssgiStepSize); loadFloat("ssgiThickness", ppSettings.ssgiThickness); loadInt("blurRange", ppSettings.blurRange);
        loadBool("autoExposure", ppSettings.autoExposure); loadFloat("manualExposure", ppSettings.manualExposure); loadFloat("exposureCompensation", ppSettings.exposureCompensation); loadFloat("minBrightness", ppSettings.minBrightness); loadFloat("maxBrightness", ppSettings.maxBrightness);
        loadFloat("contrast", ppSettings.contrast); loadFloat("saturation", ppSettings.saturation); loadFloat("temperature", ppSettings.temperature); loadFloat("gamma", ppSettings.gamma);
        loadBool("enableVignette", ppSettings.enableVignette); loadFloat("vignetteIntensity", ppSettings.vignetteIntensity);
        loadBool("enableChromaticAberration", ppSettings.enableChromaticAberration); loadFloat("caIntensity", ppSettings.caIntensity);
        loadBool("enableBloom", ppSettings.enableBloom); loadFloat("bloomThreshold", ppSettings.bloomThreshold); loadFloat("bloomIntensity", ppSettings.bloomIntensity); loadInt("bloomBlurIterations", ppSettings.bloomBlurIterations);
        loadBool("enableLensFlares", ppSettings.enableLensFlares); loadFloat("flareIntensity", ppSettings.flareIntensity); loadFloat("ghostDispersal", ppSettings.ghostDispersal); loadInt("ghosts", ppSettings.ghosts);
        loadBool("enableGodRays", ppSettings.enableGodRays); loadFloat("godRaysIntensity", ppSettings.godRaysIntensity);
        loadBool("enableFilmGrain", ppSettings.enableFilmGrain); loadFloat("grainIntensity", ppSettings.grainIntensity);
        loadBool("enableSharpen", ppSettings.enableSharpen); loadFloat("sharpenIntensity", ppSettings.sharpenIntensity);
        loadBool("enableDoF", ppSettings.enableDoF); loadFloat("focusDistance", ppSettings.focusDistance); loadFloat("focusRange", ppSettings.focusRange); loadFloat("bokehSize", ppSettings.bokehSize);
        loadBool("enableMotionBlur", ppSettings.enableMotionBlur); loadFloat("mbStrength", ppSettings.mbStrength);
        loadBool("enableFog", ppSettings.enableFog); loadFloat("fogDensity", ppSettings.fogDensity); loadFloat("fogHeightFalloff", ppSettings.fogHeightFalloff); loadFloat("fogBaseHeight", ppSettings.fogBaseHeight);
        loadFloat("inscatterPower", ppSettings.inscatterPower); loadFloat("inscatterIntensity", ppSettings.inscatterIntensity);
                if (j.contains("fogColor") && j["fogColor"].is_array()) {
            for (int i = 0; i < 3; i++) ppSettings.fogColor[i] = j["fogColor"][i];
        }
        if (j.contains("inscatterColor") && j["inscatterColor"].is_array()) {
            for (int i = 0; i < 3; i++) ppSettings.inscatterColor[i] = j["inscatterColor"][i];
        }
    }
    void DrawPostProcessContent() {
        ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Post Processing Settings");
        ImGui::Separator(); ImGui::Spacing();
        if (ImGui::BeginTabBar("PP_Tabs")) {
                        if (ImGui::BeginTabItem("Lighting")) {
                if (ImGui::CollapsingHeader("SSAO (Ambient Occlusion)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable SSAO", &ppSettings.enableSSAO);
                    if (ppSettings.enableSSAO) {
                        ImGui::SliderFloat("Radius##ssao", &ppSettings.ssaoRadius, 0.1f, 3.0f);
                        ImGui::SliderFloat("Bias##ssao", &ppSettings.ssaoBias, 0.001f, 0.2f);
                        ImGui::SliderFloat("Intensity##ssao", &ppSettings.ssaoIntensity, 0.1f, 10.0f);
                        ImGui::SliderFloat("Power##ssao", &ppSettings.ssaoPower, 1.0f, 8.0f);
                    }
                }
                if (ImGui::CollapsingHeader("SSGI (Global Illumination)", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable SSGI", &ppSettings.enableSSGI);
                    if (ppSettings.enableSSGI) {
                        ImGui::SliderInt("Ray Count", &ppSettings.ssgiRayCount, 1, 32);
                        ImGui::SliderFloat("Step Size", &ppSettings.ssgiStepSize, 0.05f, 2.0f);
                        ImGui::SliderFloat("Thickness", &ppSettings.ssgiThickness, 0.01f, 2.0f);
                        ImGui::SliderInt("Blur Range", &ppSettings.blurRange, 1, 10);
                    }
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Shadows")) {
                            if (ImGui::CollapsingHeader("SSCS (Contact Shadows)", ImGuiTreeNodeFlags_DefaultOpen)) {
                                ImGui::Checkbox("Enable SSCS", &ppSettings.enableContactShadows);
                                if (ppSettings.enableContactShadows) {
                                    ImGui::SliderFloat("Ray Length", &ppSettings.contactShadowLength, 0.01f, 0.5f);
                                    ImGui::SliderInt("Ray Steps", &ppSettings.contactShadowSteps, 4, 64);
                                    ImGui::SliderFloat("Ray Thickness", &ppSettings.contactShadowThickness, 0.01f, 0.5f);
                                }

                                ImGui::EndTabItem();
                            }
                        }
                        if (ImGui::BeginTabItem("Effects")) {
                if (ImGui::CollapsingHeader("Bloom & Lens Flares", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Bloom", &ppSettings.enableBloom);
                    if (ppSettings.enableBloom) {
                        ImGui::SliderFloat("Threshold##bloom", &ppSettings.bloomThreshold, 0.0f, 5.0f);
                        ImGui::SliderFloat("Intensity##bloom", &ppSettings.bloomIntensity, 0.0f, 5.0f);
                        ImGui::SliderInt("Blur Iterations", &ppSettings.bloomBlurIterations, 1, 15);
                    }
                    ImGui::Separator();
                    ImGui::Checkbox("Enable Lens Flares", &ppSettings.enableLensFlares);
                    if (ppSettings.enableLensFlares) {
                        ImGui::SliderFloat("Flare Intensity", &ppSettings.flareIntensity, 0.0f, 5.0f);
                        ImGui::SliderFloat("Ghost Dispersal", &ppSettings.ghostDispersal, 0.01f, 1.0f);
                        ImGui::SliderInt("Ghosts Count", &ppSettings.ghosts, 1, 10);
                    }
                }
                if (ImGui::CollapsingHeader("Screen FX", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("God Rays", &ppSettings.enableGodRays);
                    if (ppSettings.enableGodRays) ImGui::SliderFloat("Rays Power", &ppSettings.godRaysIntensity, 0.0f, 3.0f);
                    ImGui::Checkbox("Film Grain", &ppSettings.enableFilmGrain);
                    if (ppSettings.enableFilmGrain) ImGui::SliderFloat("Grain Strength", &ppSettings.grainIntensity, 0.0f, 0.2f);
                    ImGui::Checkbox("Vignette", &ppSettings.enableVignette);
                    if (ppSettings.enableVignette) ImGui::SliderFloat("Vignette Intensity", &ppSettings.vignetteIntensity, 0.1f, 2.0f);
                    ImGui::Checkbox("Chromatic Aberration", &ppSettings.enableChromaticAberration);
                    if (ppSettings.enableChromaticAberration) ImGui::SliderFloat("CA Intensity", &ppSettings.caIntensity, 0.001f, 0.55f);
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Camera & Fog")) {
                if (ImGui::CollapsingHeader("Depth of Field", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable DoF", &ppSettings.enableDoF);
                    if (ppSettings.enableDoF) {
                        ImGui::SliderFloat("Focus Dist", &ppSettings.focusDistance, 0.1f, 100.0f);
                        ImGui::SliderFloat("Focus Range", &ppSettings.focusRange, 0.1f, 50.0f);
                        ImGui::SliderFloat("Bokeh Size", &ppSettings.bokehSize, 0.0f, 10.0f);
                    }
                }
                if (ImGui::CollapsingHeader("Atmospheric Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Fog", &ppSettings.enableFog);
                    if (ppSettings.enableFog) {
                        ImGui::ColorEdit3("Fog Color", ppSettings.fogColor);
                        ImGui::ColorEdit3("Sun Inscatter Color", ppSettings.inscatterColor);
                        ImGui::SliderFloat("Density", &ppSettings.fogDensity, 0.001f, 0.2f);
                        ImGui::SliderFloat("Height Falloff", &ppSettings.fogHeightFalloff, 0.01f, 1.0f);
                        ImGui::SliderFloat("Base Height", &ppSettings.fogBaseHeight, -50.0f, 50.0f);
                        ImGui::SliderFloat("Sun Inscatter Power", &ppSettings.inscatterPower, 1.0f, 32.0f);
                        ImGui::SliderFloat("Sun Inscatter Int", &ppSettings.inscatterIntensity, 0.0f, 5.0f);
                    }
                }
                ImGui::EndTabItem();
            }
                        if (ImGui::BeginTabItem("Color Grading")) {
                if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Auto Exposure", &ppSettings.autoExposure);
                    if (ppSettings.autoExposure) {
                        ImGui::SliderFloat("Compensation", &ppSettings.exposureCompensation, 0.1f, 5.0f);
                        ImGui::SliderFloat("Min Brightness", &ppSettings.minBrightness, 0.01f, 2.0f);
                        ImGui::SliderFloat("Max Brightness", &ppSettings.maxBrightness, 1.0f, 10.0f);
                    }
                    else {
                        ImGui::SliderFloat("Manual Exp", &ppSettings.manualExposure, 0.1f, 10.0f);
                    }
                }
                if (ImGui::CollapsingHeader("Color Corrections", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::SliderFloat("Contrast", &ppSettings.contrast, 0.5f, 2.0f);
                    ImGui::SliderFloat("Saturation", &ppSettings.saturation, 0.0f, 2.0f);
                    ImGui::SliderFloat("Color Temp (K)", &ppSettings.temperature, 2000.0f, 12000.0f);
                    ImGui::SliderFloat("Gamma", &ppSettings.gamma, 1.0f, 2.8f);
                }
                if (ImGui::CollapsingHeader("Sharpening", ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::Checkbox("Enable Sharpen", &ppSettings.enableSharpen);
                    if (ppSettings.enableSharpen) ImGui::SliderFloat("Sharpness", &ppSettings.sharpenIntensity, 0.0f, 2.0f);
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        if (ImGui::Button("💾 SAVE SETTINGS", ImVec2(-1, 40))) SavePostProcessSettings();
    }
    void DrawTextureProperty(const char* label, char* pathBuffer, size_t bufferSize) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        // Так как .bhtex - это сжатый бинарник видеокарты, stb_image не сможет сделать из него миниатюру.
        // Поэтому мы просто показываем красивую иконку текстуры.
        GLuint texID = GetFileIcon(".bhtex", false);

        if (texID != 0 && strlen(pathBuffer) > 0)
            ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(64, 64));
        else
            ImGui::Button("NO TEX", ImVec2(64, 64));

        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextWrapped("%s", strlen(pathBuffer) > 0 ? pathBuffer : "None");

        if (ImGui::Button("Select Texture...")) ImGui::OpenPopup("TexturePickerPopup");
        ImGui::EndGroup();

        if (ImGui::BeginPopup("TexturePickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Textures:");
            ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();

                    // --- ТЕПЕРЬ ИЩЕМ ТОЛЬКО .bhtex ---
                    if (ext == ".bhtex") {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string();
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) {
                            strcpy_s(pathBuffer, bufferSize, relPath.c_str());
                        }
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    void DrawPropertiesWindow(Render& render) {
        if (!showProperties) return;
        ImGui::Begin("Properties", &showProperties);
        std::string activeAsset = GetPrimarySelection();
                if (selectedAssets.size() > 1) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Multiple items selected (%d)", (int)selectedAssets.size());
            ImGui::End(); return;
        }
        if (activeAsset.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Select an asset in Content Browser");
            ImGui::End(); return;
        }
        fs::path p(activeAsset); std::string ext = p.extension().string(); std::string filename = p.filename().string();
        if (filename == "postprocess.json") DrawPostProcessContent();
        else if (ext == ".png" || ext == ".jpg") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Image: %s", filename.c_str()); ImGui::Separator();
            GLuint texID = GetImageThumbnail(activeAsset); if (texID != 0) ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(200.0f, 200.0f));
        }
        else if (ext == ".bhtex") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Texture Asset: %s", filename.c_str());
            ImGui::Separator();

            // Загружаем миниатюру, если она есть
            GLuint texID = GetImageThumbnail(activeAsset);
            if (texID != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texID, ImVec2(128.0f, 128.0f));
                ImGui::Spacing();
            }

            // 1. Читаем текущие настройки прямо из файла
            BHTexHeader header;
            if (TextureImporter::ReadHeader(activeAsset, header)) {
                ImGui::TextDisabled("Resolution: %d x %d", header.width, header.height);
                ImGui::TextDisabled("Mipmaps: %d", header.mipCount);
                ImGui::TextDisabled("Compression: %s", header.format == 1 ? "BC3 (DXT5 - Alpha)" : "BC1 (DXT1 - No Alpha)");
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

                bool settingsChanged = false;

                // Массивы для выпадающих списков
                const char* wrapNames[] = { "Repeat", "Clamp To Edge", "Mirrored Repeat" };
                uint32_t wrapValues[] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };

                const char* minNames[] = { "Linear Mipmap Linear", "Nearest Mipmap Nearest", "Linear", "Nearest" };
                uint32_t minValues[] = { GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR, GL_NEAREST };

                const char* magNames[] = { "Linear", "Nearest" };
                uint32_t magValues[] = { GL_LINEAR, GL_NEAREST };

                // Хелпер, чтобы найти нужный индекс по значению из файла
                auto getIndex = [](uint32_t val, uint32_t* arr, int size) {
                    for (int i = 0; i < size; i++) if (arr[i] == val) return i;
                    return 0;
                    };

                // --- ОТРИСОВКА ИНТЕРФЕЙСА ---
                ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.9f, 1.0f), "Advanced Settings");

                int currentWrapS = getIndex(header.wrapS, wrapValues, 3);
                if (ImGui::Combo("Wrap S (U)", &currentWrapS, wrapNames, 3)) { header.wrapS = wrapValues[currentWrapS]; settingsChanged = true; }

                int currentWrapT = getIndex(header.wrapT, wrapValues, 3);
                if (ImGui::Combo("Wrap T (V)", &currentWrapT, wrapNames, 3)) { header.wrapT = wrapValues[currentWrapT]; settingsChanged = true; }

                ImGui::Spacing();

                int currentMin = getIndex(header.minFilter, minValues, 4);
                if (ImGui::Combo("Min Filter", &currentMin, minNames, 4)) { header.minFilter = minValues[currentMin]; settingsChanged = true; }

                int currentMag = getIndex(header.magFilter, magValues, 2);
                if (ImGui::Combo("Mag Filter", &currentMag, magNames, 2)) { header.magFilter = magValues[currentMag]; settingsChanged = true; }


                // 2. Если пользователь дернул ползунок - мгновенно перезаписываем файл!
                if (settingsChanged) {
                    TextureImporter::UpdateHeader(activeAsset, header);
                    // В будущем мы добавим сюда вызов обновления текстуры в видеопамяти, 
                    // если она прямо сейчас используется на какой-то 3D-модели!
                }
            }
        }
        else if (ext == ".bhmat") {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Material Settings: %s", filename.c_str());
            ImGui::Separator(); ImGui::Spacing();

            DrawTextureProperty("Albedo Map", editAlbedo, sizeof(editAlbedo)); ImGui::Spacing();
            DrawTextureProperty("Normal Map", editNormal, sizeof(editNormal)); ImGui::Spacing();
            DrawTextureProperty("Height Map", editHeight, sizeof(editHeight)); ImGui::Spacing();
            DrawTextureProperty("Metallic Map", editMetallic, sizeof(editMetallic)); ImGui::Spacing();
            DrawTextureProperty("Roughness Map", editRoughness, sizeof(editRoughness)); ImGui::Spacing();
            DrawTextureProperty("AO Map", editAO, sizeof(editAO));

            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            if (ImGui::Button("💾 SAVE MATERIAL", ImVec2(-1, 40))) {
                render.isSceneDirty = true;
                std::ifstream fileIn(activeAsset);
                json j;
                if (fileIn.is_open()) {
                    try { fileIn >> j; }
                    catch (...) {}
                    fileIn.close();
                }

                // Безопасный помощник для сохранения
                auto saveTex = [&](const std::string& key, char* buffer) {
                    if (!j.contains("textures") || !j["textures"].is_object()) {
                        j["textures"] = json::object(); // Если объекта нет, создаем его
                    }

                    if (strlen(buffer) > 0) {
                        j["textures"][key] = buffer;
                    }
                    else {
                        if (j["textures"].contains(key)) {
                            j["textures"].erase(key);
                        }
                    }
                    };

                saveTex("albedo", editAlbedo);
                saveTex("normal", editNormal);
                saveTex("height", editHeight);
                saveTex("ao", editAO);
                saveTex("metallic", editMetallic);
                saveTex("roughness", editRoughness);

                std::ofstream fileOut(activeAsset);
                fileOut << j.dump(4);
                fileOut.close();

                // Обновляем материал в памяти (безопасно)
                Material* activeMat = nullptr;
                for (auto& pair : Serializer::loadedMaterials) {
                    if (fs::path(pair.first) == fs::path(activeAsset)) {
                        activeMat = pair.second; break;
                    }
                }

                if (activeMat != nullptr) {
                    activeMat->setAlbedo(strlen(editAlbedo) > 0 ? (projectDirectory.string() + "/" + editAlbedo) : "");
                    activeMat->setNormal(strlen(editNormal) > 0 ? (projectDirectory.string() + "/" + editNormal) : "");
                    activeMat->setHeight(strlen(editHeight) > 0 ? (projectDirectory.string() + "/" + editHeight) : "");
                    activeMat->setAO(strlen(editAO) > 0 ? (projectDirectory.string() + "/" + editAO) : "");
                    activeMat->setMetallic(strlen(editMetallic) > 0 ? (projectDirectory.string() + "/" + editMetallic) : "");
                    activeMat->setRoughness(strlen(editRoughness) > 0 ? (projectDirectory.string() + "/" + editRoughness) : "");
                }
            }
        }
        else ImGui::Text("File: %s", filename.c_str());
        ImGui::End();
    }
    bool DrawAssetPicker(const char* label, std::string& outPath, const std::vector<std::string>& extensions) {
        bool changed = false; ImGui::PushID(label); ImGui::Text("%s", label); ImGui::BeginGroup();
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", outPath.empty() ? "None" : outPath.c_str());
        if (ImGui::Button("Select Asset...")) ImGui::OpenPopup("AssetPickerPopup"); ImGui::EndGroup();
        if (ImGui::BeginPopup("AssetPickerPopup")) {
            ImGui::TextColored(ImVec4(0.26f, 0.59f, 0.98f, 1.0f), "Available Assets:"); ImGui::Separator();
            for (auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string(); bool match = false;
                    for (const auto& e : extensions) { if (ext == e) match = true; }
                    if (match) {
                        std::string relPath = fs::relative(entry.path(), projectDirectory).string(); std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        if (ImGui::Selectable(relPath.c_str())) { outPath = relPath; changed = true; }
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID(); return changed;
    }
    // 1. Хелпер для получения реальной позиции в мире (для дочерних объектов)
    glm::mat4 GetWorldMatrix(entt::registry& registry, entt::entity entity) {
        if (!registry.all_of<TransformComponent>(entity)) return glm::mat4(1.0f);

        glm::mat4 world = registry.get<TransformComponent>(entity).transform.matrix;
        entt::entity curr = entity;

        while (registry.all_of<HierarchyComponent>(curr)) {
            curr = registry.get<HierarchyComponent>(curr).parent;
            if (curr != entt::null && registry.all_of<TransformComponent>(curr)) {
                // Умножаем на матрицу родителя
                world = registry.get<TransformComponent>(curr).transform.matrix * world;
            }
            else {
                break;
            }
        }
        return world;
    }

    // 2. Идеальный Ray-OBB каст через GLM
    // 2. Идеальный Ray-OBB каст (Своя реализация Slab Method)
    bool TestRayOBB(glm::vec3 rayOrigin, glm::vec3 rayDir, glm::vec3 aabbMin, glm::vec3 aabbMax, glm::mat4 worldMatrix, float& tOutput) {
        // Переводим луч в локальную систему координат объекта
        glm::mat4 invModel = glm::inverse(worldMatrix);
        glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
        glm::vec3 localDir = glm::normalize(glm::vec3(invModel * glm::vec4(rayDir, 0.0f)));

        // --- МАТЕМАТИКА ПЕРЕСЕЧЕНИЯ С AABB ---
        // Получаем обратное направление луча (чтобы заменить медленное деление на быстрое умножение)
        glm::vec3 invDir = 1.0f / localDir;

        // Ищем точки входа и выхода для каждой из трех осей
        glm::vec3 t0 = (aabbMin - localOrigin) * invDir;
        glm::vec3 t1 = (aabbMax - localOrigin) * invDir;

        glm::vec3 tmin = glm::min(t0, t1);
        glm::vec3 tmax = glm::max(t0, t1);

        // Находим самое позднее время входа и самое раннее время выхода
        float tNear = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
        float tFar = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

        // Если луч промахивается мимо коробки, или коробка строго позади луча
        if (tNear > tFar || tFar < 0.0f) {
            return false;
        }

        // Если камера находится прямо ВНУТРИ объекта (tNear < 0), мы берем точку выхода (tFar)
        tOutput = (tNear < 0.0f) ? tFar : tNear;

        // Дополнительная проверка, чтобы точно не выделить то, что за спиной
        return tOutput > 0.0f;
    }

    // 3. Правильный луч от мыши с учетом DPI масштаба Windows
    glm::vec3 GetMouseRay(Camera& camera) {
        // Берем позицию мыши прямо из ImGui
        ImVec2 mousePos = ImGui::GetMousePos();

        // Высчитываем позицию мыши ТОЛЬКО внутри нашего окошка Сцены (от 0 до 1)
        float x = (mousePos.x - viewportBounds[0].x) / viewportSize.x;
        float y = (mousePos.y - viewportBounds[0].y) / viewportSize.y;

        // Переводим в координаты для видеокарты (от -1 до 1)
        // Ось Y у картинок обычно перевернута, поэтому 1.0f - ...
        float ndcX = (x * 2.0f) - 1.0f;
        float ndcY = 1.0f - (y * 2.0f);

        // Дальше идет твоя старая математика камер
        glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f));
        glm::mat4 invView = glm::inverse(glm::lookAt(camera.Position, camera.Position + camera.Orientation, camera.Up));

        glm::vec4 rayClip = glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
        glm::vec4 rayEye = invProj * rayClip;
        rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

        return glm::normalize(glm::vec3(invView * rayEye));
    }
    void DrawExitPrompt(Window& window, entt::registry& registry) {
        // Если пришел сигнал показать окошко — открываем его
        if (showExitPrompt) {
            ImGui::OpenPopup("Внимание");
            showExitPrompt = false;
        }

        // Центрируем окошко ровно посередине экрана
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        // Создаем Modal (оно блокирует нажатия на всё остальное, пока не ответишь)
        if (ImGui::BeginPopupModal("Внимание", NULL, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Text("У вас есть несохраненная сцена. Сохранить перед выходом?");
            ImGui::Separator();
            ImGui::Spacing();

            // Кнопка ДА
            if (ImGui::Button("Да", ImVec2(120, 0))) {
                // Вызываем твою функцию сохранения
                Serializer::SaveScene(projectDirectory.string() + "/MyLevel.bhscene", registry);
                isSceneUnsaved = false; // Теперь всё сохранено
                readyToExit = true;     // Разрешаем выход
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();

            // Кнопка НЕТ
            if (ImGui::Button("Нет", ImVec2(120, 0))) {
                readyToExit = true;     // Разрешаем выход без сохранения
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();

            // Кнопка ОТМЕНА
            if (ImGui::Button("Отмена", ImVec2(120, 0))) {
                // Просто закрываем окошко, продолжаем работать дальше
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // Если после ответа на вопрос мы решили выйти, говорим окну закрыться по-настоящему
        if (readyToExit) {
            glfwSetWindowShouldClose(window.window, GLFW_TRUE);
        }
    }
    void DrawMainMenuBar(entt::registry& registry) {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene")) { registry.clear(); selectedEntity = entt::null; undoStack.clear(); redoStack.clear(); }
                if (ImGui::MenuItem("Save Scene")) { std::cout << "Update Serializer!\n"; }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undoStack.empty())) { Undo(registry); }
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, !redoStack.empty())) { Redo(registry); }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window")) {
                ImGui::MenuItem("Scene Outliner", NULL, &showOutliner); ImGui::MenuItem("Scene Inspector", NULL, &showInspector);
                ImGui::MenuItem("Properties", NULL, &showProperties); ImGui::MenuItem("Content Browser", NULL, &showContentBrowser); ImGui::Separator();
                if (ImGui::MenuItem("Restore Defaults")) { resetLayout = true; } ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void Draw(Window& window, Camera& camera, entt::registry& registry, Render& render, PostProcessingShader postprocessingshader) {
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();

        // 1. Меню бар
        DrawMainMenuBar(registry);
        ImGuiIO& io = ImGui::GetIO();

        // =====================================================================
        // 2. ГЛАВНЫЙ ХОЛСТ (DOCKSPACE) - ОБЯЗАТЕЛЬНО ДОЛЖЕН БЫТЬ В САМОМ НАЧАЛЕ!
        // =====================================================================
        ImGuiViewport* viewport = ImGui::GetMainViewport();
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

        // Первая авто-настройка окон
        static bool first_time = true;
        if (first_time || resetLayout) {
            first_time = false; resetLayout = false;
            ImGui::DockBuilderRemoveNode(dockspace_id);
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

            auto dock_main = dockspace_id; // Центр!
            auto dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.15f, nullptr, &dock_main);
            auto dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.25f, nullptr, &dock_main);
            auto dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);

            // ВАЖНО: Привязываем "Сцену" в самый центр!
            ImGui::DockBuilderDockWindow("Сцена", dock_main);

            ImGui::DockBuilderDockWindow("Scene Outliner", dock_left);
            ImGui::DockBuilderDockWindow("Content Browser", dock_bottom);
            ImGui::DockBuilderDockWindow("Scene Inspector", dock_right);
            ImGui::DockBuilderDockWindow("Properties", dock_right);
            ImGui::DockBuilderFinish(dockspace_id);
        }
        ImGui::End(); // Закрываем MainDockSpace_Window

        // =====================================================================
        // 3. ОКНО СЦЕНЫ (Теперь оно ляжет правильно внутрь DockSpace)
        // =====================================================================
        ImGuiWindowFlags viewFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Сцена", nullptr, viewFlags);

        ImVec2 windowSize = ImGui::GetContentRegionAvail();
        viewportSize = windowSize;

        ImVec2 minBound = ImGui::GetWindowContentRegionMin();
        ImVec2 maxBound = ImGui::GetWindowContentRegionMax();
        ImVec2 windowPos = ImGui::GetWindowPos();
        viewportBounds[0] = ImVec2(minBound.x + windowPos.x, minBound.y + windowPos.y);
        viewportBounds[1] = ImVec2(maxBound.x + windowPos.x, maxBound.y + windowPos.y);

        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)postprocessingshader.finalSceneTexture, windowSize, ImVec2(0, 1), ImVec2(1, 0));

        ImGui::SetCursorScreenPos(cursorPos);
        ImGui::InvisibleButton("##ViewportButton", windowSize);
        isViewportHovered = ImGui::IsItemHovered();

        // 4. НАСТРОЙКА ГИЗМО - ТЕПЕРЬ ОНО РАБОТАЕТ ТОЛЬКО ВНУТРИ СЦЕНЫ!
        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList()); // Рисуем в контексте текущего окна
        // ИДЕАЛЬНЫЕ КООРДИНАТЫ ДЛЯ ГИЗМО:
        ImGuizmo::SetRect(viewportBounds[0].x, viewportBounds[0].y, viewportSize.x, viewportSize.y);

        ImGui::End();
        ImGui::PopStyleVar();

        // =====================================================================
        // 5. ГОРЯЧИЕ КЛАВИШИ, ЛУЧ И ЛОГИКА ГИЗМО
        // =====================================================================
        if (!ImGui::IsAnyItemActive()) {
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Undo(registry);
            if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) Redo(registry);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedEntity != entt::null) {
                SaveState(registry);
                entt::entity parent = entt::null;
                if (registry.all_of<HierarchyComponent>(selectedEntity)) parent = registry.get<HierarchyComponent>(selectedEntity).parent;
                entt::entity newEnt = CloneHierarchy(registry, selectedEntity, parent);
                if (parent != entt::null) registry.get<HierarchyComponent>(parent).children.push_back(newEnt);
                selectedEntity = newEnt;
            }
        }

        static ImGuizmo::OPERATION currentGizmoOperation = ImGuizmo::TRANSLATE;
        static ImGuizmo::MODE currentGizmoMode = ImGuizmo::WORLD;
        if (ImGui::IsKeyPressed(ImGuiKey_Q)) currentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) currentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) currentGizmoOperation = ImGuizmo::SCALE;

        glm::mat4 view = camera.GetViewMatrix();
        glm::mat4 proj = camera.GetProjectionMatrix(45.0f, 0.1f, 1000.0f);

        // --- МЫШКА (ЛУЧ И ВЫДЕЛЕНИЕ) ---
        if (ImGui::IsMouseClicked(0) && isViewportHovered && !ImGuizmo::IsOver()) {
            glm::vec3 rayOrigin = camera.Position;
            glm::vec3 rayDir = GetMouseRay(camera);
            float closestT = 100000.0f;
            entt::entity hitIndex = entt::null;

            auto pickView = registry.view<TransformComponent>();
            pickView.each([&](entt::entity entity, TransformComponent& tComp) {
                float t;
                glm::vec3 aabbMin = glm::vec3(-1.0f);
                glm::vec3 aabbMax = glm::vec3(1.0f);

                if (registry.all_of<PhysicsComponent>(entity)) {
                    auto& phys = registry.get<PhysicsComponent>(entity);
                    if (phys.colliderType == ColliderType::Box) {
                        aabbMin = -phys.extents;
                        aabbMax = phys.extents;
                    }
                    else if (phys.colliderType == ColliderType::Sphere) {
                        aabbMin = glm::vec3(-phys.radius);
                        aabbMax = glm::vec3(phys.radius);
                    }
                }

                glm::mat4 worldMatrix = GetWorldMatrix(registry, entity);
                if (TestRayOBB(rayOrigin, rayDir, aabbMin, aabbMax, worldMatrix, t)) {
                    if (t < closestT) {
                        closestT = t;
                        hitIndex = entity;
                    }
                }
                });

            selectedEntity = hitIndex;
            if (hitIndex != entt::null) {
                render.isSceneDirty = true;
                ImGuizmo::RecomposeMatrixFromComponents(
                    glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.position),
                    glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.rotation),
                    glm::value_ptr(registry.get<TransformComponent>(selectedEntity).transform.scale),
                    glm::value_ptr(model));
            }
        }

        if (selectedEntity != entt::null && registry.valid(selectedEntity) && registry.all_of<TransformComponent>(selectedEntity)) {
            auto& tComp = registry.get<TransformComponent>(selectedEntity);

            glm::mat4 localMatrix;
            ImGuizmo::RecomposeMatrixFromComponents(
                glm::value_ptr(tComp.transform.position),
                glm::value_ptr(tComp.transform.rotation),
                glm::value_ptr(tComp.transform.scale),
                glm::value_ptr(localMatrix)
            );

            glm::mat4 worldMatrix = localMatrix;
            glm::mat4 parentWorldMatrix = glm::mat4(1.0f);

            if (registry.all_of<HierarchyComponent>(selectedEntity)) {
                entt::entity parent = registry.get<HierarchyComponent>(selectedEntity).parent;
                if (parent != entt::null && registry.all_of<TransformComponent>(parent)) {
                    parentWorldMatrix = registry.get<TransformComponent>(parent).transform.matrix;
                    worldMatrix = parentWorldMatrix * localMatrix;
                }
            }

            if (ImGuizmo::IsUsing() && !wasUsingGizmo) SaveState(registry);
            wasUsingGizmo = ImGuizmo::IsUsing();

            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), currentGizmoOperation, currentGizmoMode, glm::value_ptr(worldMatrix));

            if (ImGuizmo::IsUsing()) {
                render.isSceneDirty = true;
                isSceneUnsaved = true; // Сцена изменилась!

                glm::mat4 newLocalMatrix = glm::inverse(parentWorldMatrix) * worldMatrix;

                ImGuizmo::DecomposeMatrixToComponents(
                    glm::value_ptr(newLocalMatrix),
                    glm::value_ptr(tComp.transform.position),
                    glm::value_ptr(tComp.transform.rotation),
                    glm::value_ptr(tComp.transform.scale)
                );

                tComp.transform.updatematrix = true;

                if (registry.all_of<PhysicsComponent>(selectedEntity)) {
                    registry.get<PhysicsComponent>(selectedEntity).updatePhysicsTransform = true;
                }
            }
        }

        // =====================================================================
        // 6. ОТРИСОВКА ОСТАЛЬНЫХ ОКОН
        // =====================================================================
        DrawExitPrompt(window, registry);
        DrawToolbar(registry, render);
        DrawSceneOutliner(registry, io);
        DrawSceneInspector(registry, render);
        DrawContentBrowser();
        DrawPropertiesWindow(render);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
};
#endif