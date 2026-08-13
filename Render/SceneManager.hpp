#pragma once
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
        static void saveScene(flecs::world& world, const std::string& filePath)
        {
            json sceneJson;
            auto entitiesArray = json::array();

            world.each<IDComponent>([&](flecs::entity entity, IDComponent& idComp)
            {
                json eJson;
                eJson["id"] = idComp.ID;

                if (entity.has<TagComponent>()) {
                    eJson["tag"] = entity.get<TagComponent>()->name;
                }

                if (entity.has<Position3>() && entity.has<RotationEuler>() && entity.has<Scale3>()) {
                    const Position3& pos = *entity.get<Position3>();
                    const RotationEuler& rot = *entity.get<RotationEuler>();
                    const Scale3& scale = *entity.get<Scale3>();
                    eJson["transform"] = {
                        {"pos", {pos.x, pos.y, pos.z}},
                        {"rot", {rot.x, rot.y, rot.z}},
                        {"scale", {scale.x, scale.y, scale.z}}
                    };
                }

                if (entity.has<HierarchyComponent>()) {
                    const HierarchyComponent& hc = *entity.get<HierarchyComponent>();
                    eJson["hierarchy"] = {
                        {"parentID", hc.parentID},
                        {"childrenIDs", hc.childrenIDs}
                    };
                }

                if (entity.has<LightComponent>()) {
                    const LightComponent& lc = *entity.get<LightComponent>();
                    eJson["light"] = {
                        {"type", static_cast<int>(lc.light.type)},
                        {"color", {lc.light.color.r, lc.light.color.g, lc.light.color.b}},
                        {"intensity", lc.light.intensity},
                        {"radius", lc.light.radius},
                        {"castShadows", lc.light.castShadows}
                    };
                }

                if (entity.has<MeshComponent>()) {
                    MeshComponent& mc = *entity.get_mut<MeshComponent>();
                    
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
                        {"castShadow", mc.castShadow},
                        {"skeletonPath", mc.skeletonPath},
                        {"animationPath", mc.animationPath},
                        {"animationTime", mc.animationTime}
                    };
                }

                if (entity.has<ReflectionProbeComponent>()) {
                    const ReflectionProbeComponent& pc = *entity.get<ReflectionProbeComponent>();
                    eJson["probe"] = {
                        {"radius", pc.radius},
                        {"resolution", pc.resolution}
                    };
                }

                entitiesArray.push_back(eJson);
            });

            sceneJson["entities"] = entitiesArray;

            std::ofstream file(filePath);
            if (file.is_open()) {
                file << sceneJson.dump(4);
                std::cout << "[SUCCESS] Сцена сохранена в " << filePath << "\n";
            }
        }

        static void loadScene(BurnhopeDevice& device, flecs::world& world, const std::string& filePath)
        {
            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::cerr << "[ERROR] Не удалось открыть сцену: " << filePath << "\n";
                return;
            }

            json sceneJson;
            file >> sceneJson;
            world.reset();

            for (const auto& eJson : sceneJson["entities"])
            {
                auto entity = world.entity();

                uint64_t id = eJson.value("id", generateRandomID());
                entity.set<IDComponent>(IDComponent{id});

                if (eJson.contains("tag")) {
                    entity.set<TagComponent>({eJson["tag"].get<std::string>()});
                }

                if (eJson.contains("transform")) {
                    auto& p = eJson["transform"]["pos"];
                    auto& r = eJson["transform"]["rot"];
                    auto& s = eJson["transform"]["scale"];
                    entity.set<Position3>({p[0], p[1], p[2]});
                    entity.set<RotationEuler>({r[0], r[1], r[2]});
                    entity.set<Scale3>({s[0], s[1], s[2]});
                    entity.set<LocalMatrix>({glm::mat4(1.0f), 1});
                    if (LocalMatrix* matrix = entity.get_mut<LocalMatrix>()) {
                        transform::markDirty(*matrix);
                    }
                }

                if (eJson.contains("hierarchy")) {
                    HierarchyComponent hc;
                    hc.parentID = eJson["hierarchy"].value("parentID", 0ULL);
                    for (auto& childId : eJson["hierarchy"]["childrenIDs"]) {
                        hc.childrenIDs.push_back(childId.get<uint64_t>());
                    }
                    entity.set<HierarchyComponent>(hc);
                }

                if (eJson.contains("light")) {
                    LightComponent lc;
                    auto& lData = eJson["light"];
                    lc.light.type = static_cast<LightType>(lData.value("type", 0));
                    lc.light.color = {lData["color"][0], lData["color"][1], lData["color"][2]};
                    lc.light.intensity = lData.value("intensity", 1.0f);
                    lc.light.radius = lData.value("radius", 10.0f);
                    lc.light.castShadows = lData.value("castShadows", true);
                    lc.light.enable = true;
                    entity.set<LightComponent>(lc);
                }

                if (eJson.contains("mesh")) {
                    MeshComponent mc;
                    mc.modelPath = eJson["mesh"].value("modelPath", "");
                    mc.skeletonPath = eJson["mesh"].value("skeletonPath", "");
                    mc.animationPath = eJson["mesh"].value("animationPath", "");
                    mc.animationTime = eJson["mesh"].value("animationTime", 0.0f);
                    mc.isStatic = eJson["mesh"].value("isStatic", false);
                    mc.isVisible = eJson["mesh"].value("isVisible", true);
                    mc.castShadow = eJson["mesh"].value("castShadow", true);

                    if (!mc.modelPath.empty()) {
                        try {
                            mc.model = BurnhopeModel::createModelFromFile(device, mc.modelPath);
                        } catch (const std::exception& e) {
                            std::cerr << "[ERROR] Failed to load model: " << mc.modelPath << " | " << e.what() << "\n";
                        }
                    }

                    for (const auto& matPath : eJson["mesh"]["materialPaths"]) {
                        std::string path = matPath.get<std::string>();
                        mc.materialPaths.push_back(path);
                        auto mat = Material::loadFromJson(device, path);
                        mc.materials.push_back(mat);
                    }

                    if (mc.model && mc.materials.size() < mc.model->getSubMeshes().size()) {
                        mc.materials.resize(mc.model->getSubMeshes().size(), nullptr);
                    }
                    if (mc.model && mc.materialPaths.size() < mc.model->getSubMeshes().size()) {
                        mc.materialPaths.resize(mc.model->getSubMeshes().size(), "");
                    }

                    entity.set<MeshComponent>(mc);
                }

                if (eJson.contains("probe")) {
                    ReflectionProbeComponent pc;
                    pc.radius = eJson["probe"].value("radius", 10.0f);
                    pc.resolution = eJson["probe"].value("resolution", 256);
                    pc.updateNeeded = true;
                    entity.set<ReflectionProbeComponent>(pc);
                }
            }
            std::cout << "[SUCCESS] Сцена загружена: " << filePath << "\n";
        }
    };
}
