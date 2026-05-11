#pragma once
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include "../Utils/Components.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace burnhope
{
    class SceneManager
    {
    public:
        static void saveScene(entt::registry& registry, const std::string& filePath)
        {
            json sceneJson;
            auto entitiesArray = json::array();

            auto view = registry.view<IDComponent>();
            for (auto entity : view)
            {
                json eJson;
                eJson["id"] = view.get<IDComponent>(entity).ID;

                if (registry.any_of<TagComponent>(entity)) {
                    eJson["tag"] = registry.get<TagComponent>(entity).name;
                }

                if (registry.any_of<TransformComponent>(entity)) {
                    auto& tc = registry.get<TransformComponent>(entity).transform;
                    eJson["transform"] = {
                        {"pos", {tc.position.x, tc.position.y, tc.position.z}},
                        {"rot", {tc.rotation.x, tc.rotation.y, tc.rotation.z}},
                        {"scale", {tc.scale.x, tc.scale.y, tc.scale.z}}
                    };
                }

                if (registry.any_of<HierarchyComponent>(entity)) {
                    auto& hc = registry.get<HierarchyComponent>(entity);
                    eJson["hierarchy"] = {
                        {"parentID", hc.parentID},
                        {"childrenIDs", hc.childrenIDs}
                    };
                }

                if (registry.any_of<LightComponent>(entity)) {
                    auto& lc = registry.get<LightComponent>(entity).light;
                    eJson["light"] = {
                        {"type", static_cast<int>(lc.type)},
                        {"color", {lc.color.r, lc.color.g, lc.color.b}},
                        {"intensity", lc.intensity},
                        {"radius", lc.radius},
                        {"castShadows", lc.castShadows}
                    };
                }

                if (registry.any_of<MeshComponent>(entity)) {
                    auto& mc = registry.get<MeshComponent>(entity);
                    
                    if (!fs::exists("materials")) fs::create_directory("materials");

                    for (size_t i = 0; i < mc.materials.size(); ++i) {
                        if (i >= mc.materialPaths.size()) {
                            mc.materialPaths.push_back("materials/mat_" + std::to_string(mc.materials[i]->ID) + ".json");
                        }
                        mc.materials[i]->saveToJson(mc.materialPaths[i]);
                    }

                    eJson["mesh"] = {
                        {"modelPath", mc.modelPath},
                        {"materialPaths", mc.materialPaths},
                        {"isStatic", mc.isStatic},
                        {"isVisible", mc.isVisible},
                        {"castShadow", mc.castShadow}
                    };
                }

                if (registry.any_of<ReflectionProbeComponent>(entity)) {
                    auto& pc = registry.get<ReflectionProbeComponent>(entity);
                    eJson["probe"] = {
                        {"radius", pc.radius},
                        {"resolution", pc.resolution}
                    };
                }

                entitiesArray.push_back(eJson);
            }

            sceneJson["entities"] = entitiesArray;

            std::ofstream file(filePath);
            if (file.is_open()) {
                file << sceneJson.dump(4);
                std::cout << "[SUCCESS] Сцена сохранена в " << filePath << "\n";
            }
        }

        static void loadScene(BurnhopeDevice& device, entt::registry& registry, const std::string& filePath)
        {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "[ERROR] Не удалось открыть сцену: " << filePath << "\n";
                return;
            }

            json sceneJson;
            file >> sceneJson;
            registry.clear();

            for (const auto& eJson : sceneJson["entities"])
            {
                auto entity = registry.create();

                uint64_t id = eJson.value("id", generateRandomID());
                registry.emplace<IDComponent>(entity, id);

                if (eJson.contains("tag")) {
                    registry.emplace<TagComponent>(entity, eJson["tag"].get<std::string>());
                }

                if (eJson.contains("transform")) {
                    auto& tc = registry.emplace<TransformComponent>(entity).transform;
                    auto& p = eJson["transform"]["pos"];
                    auto& r = eJson["transform"]["rot"];
                    auto& s = eJson["transform"]["scale"];
                    tc.position = {p[0], p[1], p[2]};
                    tc.rotation = {r[0], r[1], r[2]};
                    tc.scale = {s[0], s[1], s[2]};
                    tc.updatematrix = true;
                    tc.updateMatrixIfNeeded();
                }

                if (eJson.contains("hierarchy")) {
                    auto& hc = registry.emplace<HierarchyComponent>(entity);
                    hc.parentID = eJson["hierarchy"].value("parentID", 0ULL);
                    for (auto& childId : eJson["hierarchy"]["childrenIDs"]) {
                        hc.childrenIDs.push_back(childId.get<uint64_t>());
                    }
                }

                if (eJson.contains("light")) {
                    auto& lc = registry.emplace<LightComponent>(entity).light;
                    auto& lData = eJson["light"];
                    lc.type = static_cast<LightType>(lData.value("type", 0));
                    lc.color = {lData["color"][0], lData["color"][1], lData["color"][2]};
                    lc.intensity = lData.value("intensity", 1.0f);
                    lc.radius = lData.value("radius", 10.0f);
                    lc.castShadows = lData.value("castShadows", true);
                    lc.enable = true;
                }

                if (eJson.contains("mesh")) {
                    auto& mc = registry.emplace<MeshComponent>(entity);
                    mc.modelPath = eJson["mesh"].value("modelPath", "");
                    mc.isStatic = eJson["mesh"].value("isStatic", false);
                    mc.isVisible = eJson["mesh"].value("isVisible", true);
                    mc.castShadow = eJson["mesh"].value("castShadow", true);

                    if (!mc.modelPath.empty()) {
                        mc.model = BurnhopeModel::createModelFromFile(device, mc.modelPath);
                    }

                    for (const auto& matPath : eJson["mesh"]["materialPaths"]) {
                        std::string path = matPath.get<std::string>();
                        mc.materialPaths.push_back(path);
                        auto mat = Material::loadFromJson(device, path);
                    mc.materials.push_back(mat); // добавляем даже если nullptr
                }

                if (mc.model && mc.materials.size() < mc.model->getSubMeshes().size()) {
                    mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                }
                if (mc.model && mc.materialPaths.size() < mc.model->getSubMeshes().size()) {
                    mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                }
                }

                if (eJson.contains("probe")) {
                    auto& pc = registry.emplace<ReflectionProbeComponent>(entity);
                    pc.radius = eJson["probe"].value("radius", 10.0f);
                    pc.resolution = eJson["probe"].value("resolution", 256);
                    pc.updateNeeded = true;
                }
            }
            std::cout << "[SUCCESS] Сцена загружена: " << filePath << "\n";
        }
    };
}