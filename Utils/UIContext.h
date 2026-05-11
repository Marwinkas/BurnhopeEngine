#pragma once
#include <entt/entt.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "Components.hpp"

namespace burnhope {
    namespace fs = std::filesystem;

    struct RenderSettings {
        int rtMaxBounces = 1;
        bool enableRTReflections = true;
        bool enableRadianceCascades = true;
        int rcProbeGridX = 16, rcProbeGridY = 9, rcProbeGridZ = 24, rcOctaSize = 8;
        float rcBaseRayLength = 1.0f;

        bool enableSSAO = true;
        float ssaoRadius = 0.5f, ssaoBias = 0.025f, ssaoIntensity = 2.0f, ssaoPower = 2.0f;
        
        bool enableSSGI = true;
        int ssgiRayCount = 8, blurRange = 4;
        float ssgiStepSize = 0.4f, ssgiThickness = 0.5f;

        bool autoExposure = true;
        float manualExposure = 1.0f, exposureCompensation = 1.0f, minBrightness = 0.5f, maxBrightness = 3.0f;
        float contrast = 1.0f, saturation = 1.0f, gamma = 2.2f, temperature = 8000.0f;

        bool enableVignette = false, enableChromaticAberration = false, enableBloom = true, enableLensFlares = true;
        float vignetteIntensity = 0.5f, caIntensity = 0.005f, bloomThreshold = 1.0f, bloomIntensity = 1.5f;
        int bloomBlurIterations = 10, ghosts = 4;
        float flareIntensity = 0.5f, ghostDispersal = 0.3f;

        bool enableDoF = false, enableMotionBlur = false, enableFilmGrain = false, enableSharpen = false, enableFog = true;
        float focusDistance = 10.0f, focusRange = 3.0f, bokehSize = 2.0f, mbStrength = 0.5f, grainIntensity = 0.05f, sharpenIntensity = 0.5f;
        float fogDensity = 0.02f, fogHeightFalloff = 0.2f, fogBaseHeight = 0.0f;
        float fogColor[3] = {0.5f, 0.6f, 0.7f}, inscatterColor[3] = {1.0f, 0.8f, 0.5f};
        float inscatterPower = 8.0f, inscatterIntensity = 1.0f;

        float skyZenithColor[3] = {0.15f, 0.35f, 0.75f}, skyHorizonColor[3] = {0.6f, 0.7f, 0.8f};
        float sunSize = 0.005f, sunGlow = 1.5f, sunGlowSize = 0.1f;

        bool enableContactShadows = true;
        float contactShadowLength = 0.05f, contactShadowThickness = 0.1f;
        int contactShadowSteps = 16;
    };

    struct SceneSnapshot {
        std::shared_ptr<entt::registry> regCopy;
        entt::entity selectedEntity;
    };

    struct PendingDeletion {
        std::vector<std::shared_ptr<void>> objects;
        int framesRemaining;
    };

    class UIContext {
    public:
        class BurnhopeDevice* device = nullptr;
        entt::registry* registry = nullptr;
        entt::entity selectedEntity = entt::null;
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        std::string currentScenePath = "";

        fs::path projectDirectory;
        fs::path exeDirectory;
        fs::path currentDirectory;
        std::vector<fs::path> dirHistory;
        int dirHistoryIndex = -1;

        std::vector<std::string> selectedAssets;
        std::vector<std::string> clipboardPaths;
        bool isCut = false;
        std::string renamingPath = "";

        RenderSettings renderSettings;
        bool needsRebuild = false;
        bool pendingNewScene = false;
        std::string pendingSceneLoadPath = "";
        std::vector<std::shared_ptr<void>> safeDeleteQueue;
        std::vector<PendingDeletion> pendingDeletions;
        std::vector<SceneSnapshot> undoStack;
        std::vector<SceneSnapshot> redoStack;

        // Утилита для получения списка файлов нужного типа
        std::vector<std::string> GetProjectAssets(const std::vector<std::string>& extensions) {
            std::vector<std::string> result;
            if (!fs::exists(projectDirectory)) return result;
            
            for (const auto& entry : fs::recursive_directory_iterator(projectDirectory)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    for (const auto& e : extensions) {
                        if (ext == e) {
                            result.push_back(entry.path().string());
                            break;
                        }
                    }
                }
            }
            return result;
        }

        // Вспомогательные методы, перенесенные из твоего старого UI
        void SaveState() {
            auto snapReg = std::make_shared<entt::registry>();
            CopyRegistry(*registry, *snapReg);
            undoStack.push_back({snapReg, selectedEntity});
            redoStack.clear();
            if (undoStack.size() > 50) undoStack.erase(undoStack.begin());
        }

        void CopyRegistry(entt::registry& src, entt::registry& dst) {
            dst.clear();
            src.view<TagComponent>().each([&](entt::entity entity, TagComponent& tag) {
                entt::entity newEnt = dst.create(entity); 
                dst.emplace<TagComponent>(newEnt, tag);
                if (src.all_of<IDComponent>(entity)) dst.emplace<IDComponent>(newEnt, src.get<IDComponent>(entity));
                if (src.all_of<TransformComponent>(entity)) dst.emplace<TransformComponent>(newEnt, src.get<TransformComponent>(entity));
                if (src.all_of<MeshComponent>(entity)) dst.emplace<MeshComponent>(newEnt, src.get<MeshComponent>(entity));
                if (src.all_of<LightComponent>(entity)) dst.emplace<LightComponent>(newEnt, src.get<LightComponent>(entity));
                if (src.all_of<HierarchyComponent>(entity)) dst.emplace<HierarchyComponent>(newEnt, src.get<HierarchyComponent>(entity)); 
                if (src.all_of<ReflectionProbeComponent>(entity)) dst.emplace<ReflectionProbeComponent>(newEnt, src.get<ReflectionProbeComponent>(entity));
            });
        }

        entt::entity FindEntityByID(uint64_t id) {
            if (id == 0) return entt::null;
            for (auto e : registry->view<IDComponent>()) {
                if (registry->get<IDComponent>(e).ID == id) return e;
            }
            return entt::null;
        }

        void DetachFromParent(entt::entity child) {
            if (!registry->all_of<HierarchyComponent>(child) || !registry->all_of<IDComponent>(child)) return;
            auto& hc = registry->get<HierarchyComponent>(child);
            if (hc.parentID == 0) return;
            uint64_t myID = registry->get<IDComponent>(child).ID;
            entt::entity parentEnt = FindEntityByID(hc.parentID);
            if (parentEnt != entt::null && registry->all_of<HierarchyComponent>(parentEnt)) {
                auto& phc = registry->get<HierarchyComponent>(parentEnt);
                phc.childrenIDs.erase(std::remove(phc.childrenIDs.begin(), phc.childrenIDs.end(), myID), phc.childrenIDs.end());
            }
            hc.parentID = 0;
        }

        void AttachToParent(entt::entity child, entt::entity newParent) {
            if (child == newParent) return;
            DetachFromParent(child);
            if (newParent == entt::null) return;
            if (!registry->all_of<HierarchyComponent>(child)) registry->emplace<HierarchyComponent>(child);
            if (!registry->all_of<HierarchyComponent>(newParent)) registry->emplace<HierarchyComponent>(newParent);
            
            uint64_t myID = registry->get<IDComponent>(child).ID;
            uint64_t pid = registry->get<IDComponent>(newParent).ID;

            registry->get<HierarchyComponent>(child).parentID = pid;
            registry->get<HierarchyComponent>(newParent).childrenIDs.push_back(myID);
        }

        entt::entity CreateBaseEntity(const std::string& name) {
            entt::entity e = registry->create();
            registry->emplace<IDComponent>(e); 
            registry->emplace<TagComponent>(e, name);
            registry->emplace<TransformComponent>(e);
            registry->emplace<HierarchyComponent>(e);
            return e;
        }

        void DeleteEntityRecursive(entt::entity target) {
            if (!registry->valid(target)) return;
            
            if (device) vkDeviceWaitIdle(device->device());
            
            if (registry->all_of<HierarchyComponent>(target)) {
                auto children = registry->get<HierarchyComponent>(target).childrenIDs;
                for (uint64_t childID : children) DeleteEntityRecursive(FindEntityByID(childID));
            }
            DetachFromParent(target);
            if (selectedEntity == target) selectedEntity = entt::null;
            registry->destroy(target);
        }
    };
}