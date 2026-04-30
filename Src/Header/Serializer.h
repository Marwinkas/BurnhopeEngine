#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp> 
#include "Material.h"
#include "Model.h"
#include <entt/entt.hpp>
#include "Components.h"
using json = nlohmann::json;
class Serializer {
private:
    public:
                static std::unordered_map<std::string, Model*> loadedModels;
    static std::unordered_map<std::string, Material*> loadedMaterials;
    static void SaveMaterial(const std::string& filepath, Material* material, const std::string& matName) {
        json j;
        j["name"] = matName;
                                j["textures"]["albedo"] = "path_to_albedo.png";         std::ofstream file(filepath);
        file << j.dump(4);     }
    // 1. ДОБАВЛЯЕМ СТРИМЕР В АРГУМЕНТЫ (по умолчанию nullptr, чтобы старый код не сломался)
    static Material* LoadMaterial(const std::string& filepath, const std::string& basePath, TextureStreamer* streamer = nullptr) {
        // Проверяем кэш, чтобы не загружать один и тот же файл дважды
        if (loadedMaterials.find(filepath) != loadedMaterials.end()) {
            return loadedMaterials[filepath];
        }

        Material* mat = new Material();
        std::ifstream file(filepath);

        if (file.is_open()) {
            json j;
            try {
                file >> j;
            }
            catch (json::parse_error& e) {
                std::cout << "ОШИБКА ЧТЕНИЯ JSON в файле " << filepath << "\n";
                std::cout << "Подробности: " << e.what() << "\n";
                return mat;
            }

            if (j.contains("textures")) {
                auto& tex = j["textures"];
                extern TextureStreamer* globalTextureStreamer;
                // 2. ИЗМЕНЯЕМ СИГНАТУРУ ЛЯМБДЫ (добавляем TextureStreamer*)
                auto loadIfNotEmpty = [&](const std::string& key, void (Material::* func)(std::string, TextureStreamer*)) {
                    if (tex.contains(key)) {
                        std::string path = tex[key].get<std::string>();
                        if (!path.empty()) {
                            std::string fullTexPath = basePath + "/" + path;

                            // 3. ВЫЗЫВАЕМ ФУНКЦИЮ И ПЕРЕДАЕМ ЕЙ СТРИМЕР!
                            (mat->*func)(fullTexPath, globalTextureStreamer);
                        }
                    }
                    };

                loadIfNotEmpty("albedo", &Material::setAlbedo);
                loadIfNotEmpty("normal", &Material::setNormal);
                loadIfNotEmpty("height", &Material::setHeight);
                loadIfNotEmpty("metallic", &Material::setMetallic);
                loadIfNotEmpty("roughness", &Material::setRoughness);
                loadIfNotEmpty("ao", &Material::setAO);
            }
        }
        else {
            std::cout << "Ой, не удалось найти файл материала: " << filepath << std::endl;
        }

        loadedMaterials[filepath] = mat;
        return mat;
    }

    // --- НОВОЕ СОХРАНЕНИЕ СЦЕНЫ (EnTT) ---
    static void SaveScene(const std::string& filepath, entt::registry& registry) {
        json sceneJson;
        sceneJson["scene_name"] = "Burnhope Scene";
        sceneJson["objects"] = json::array();

        // Проходимся по всем сущностям в реестре
        for (auto [entity] : registry.storage<entt::entity>().each()) {
            json objJson;
            // Сохраняем текущий ID сущности, чтобы потом правильно собрать иерархию (детей и родителей)
            objJson["id"] = (uint32_t)entity;

            // 1. Имя / Тег
            if (registry.all_of<TagComponent>(entity)) {
                objJson["name"] = registry.get<TagComponent>(entity).name;
            }
            else {
                objJson["name"] = "Entity";
            }

            // 2. Трансформ
            if (registry.all_of<TransformComponent>(entity)) {
                auto& t = registry.get<TransformComponent>(entity).transform;
                objJson["transform"]["position"] = { t.position.x, t.position.y, t.position.z };
                objJson["transform"]["rotation"] = { t.rotation.x, t.rotation.y, t.rotation.z };
                objJson["transform"]["scale"] = { t.scale.x, t.scale.y, t.scale.z };
            }

            // 3. Меш и Материалы
            if (registry.all_of<MeshComponent>(entity)) {
                auto& m = registry.get<MeshComponent>(entity);
                objJson["mesh"]["isStatic"] = m.isStatic;
                objJson["mesh"]["isVisible"] = m.isVisible;
                objJson["mesh"]["castShadow"] = m.castShadow;
                objJson["mesh"]["model_path"] = m.modelPath;
                objJson["mesh"]["materials"] = m.materialPaths; // std::vector автоматически станет JSON массивом
            }

            // 4. Свет
            if (registry.all_of<LightComponent>(entity)) {
                auto& l = registry.get<LightComponent>(entity).light;
                objJson["light"]["enable"] = l.enable;
                objJson["light"]["type"] = (int)l.type;
                objJson["light"]["mobility"] = (int)l.mobility;
                objJson["light"]["color"] = { l.color.x, l.color.y, l.color.z };
                objJson["light"]["intensity"] = l.intensity;
                objJson["light"]["radius"] = l.radius;
                objJson["light"]["innerCone"] = l.innerCone;
                objJson["light"]["outerCone"] = l.outerCone;
                objJson["light"]["castShadows"] = l.castShadows;
            }

            // 5. Физика
            if (registry.all_of<PhysicsComponent>(entity)) {
                auto& p = registry.get<PhysicsComponent>(entity);
                objJson["physics"]["colliderType"] = (int)p.colliderType;
                objJson["physics"]["bodyType"] = (int)p.bodyType;
                objJson["physics"]["extents"] = { p.extents.x, p.extents.y, p.extents.z };
                objJson["physics"]["radius"] = p.radius;
                objJson["physics"]["mass"] = p.mass;
                objJson["physics"]["friction"] = p.friction;
                objJson["physics"]["restitution"] = p.restitution;
            }

            // 6. Иерархия (Родитель)
            if (registry.all_of<HierarchyComponent>(entity)) {
                auto& h = registry.get<HierarchyComponent>(entity);
                if (h.parent != entt::null) {
                    objJson["hierarchy"]["parent"] = (uint32_t)h.parent;
                }
            }

            sceneJson["objects"].push_back(objJson);
        }

        std::ofstream file(filepath);
        file << sceneJson.dump(4); // Сохраняем с красивыми отступами
    }

    // --- НОВАЯ ЗАГРУЗКА СЦЕНЫ (EnTT) ---
    static void LoadScene(const std::string& filepath, const std::string& basePath, entt::registry& registry) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cout << "[ERROR] Не удалось открыть сцену: " << filepath << "\n";
            return;
        }

        json sceneJson;
        try { file >> sceneJson; }
        catch (...) {
            std::cout << "[ERROR] Ошибка чтения JSON!\n";
            return;
        }

        // Очищаем текущую сцену перед загрузкой новой!
        registry.clear();

        // Карта для связи: Старый ID из JSON -> Новый entt::entity
        std::unordered_map<uint32_t, entt::entity> idMap;

        // ПЕРВЫЙ ПРОХОД: Создаем сущности и загружаем компоненты
        for (const auto& objJson : sceneJson["objects"]) {
            entt::entity entity = registry.create(); // Создаем пустой объект

            // Запоминаем его старый ID для иерархии
            if (objJson.contains("id")) {
                idMap[objJson["id"].get<uint32_t>()] = entity;
            }

            // 1. Имя
            std::string name = objJson.contains("name") ? objJson["name"].get<std::string>() : "Entity";
            registry.emplace<TagComponent>(entity, name);

            // 2. Трансформ
            auto& tComp = registry.emplace<TransformComponent>(entity);
            if (objJson.contains("transform")) {
                auto pos = objJson["transform"]["position"];
                auto rot = objJson["transform"]["rotation"];
                auto scl = objJson["transform"]["scale"];
                tComp.transform.position = glm::vec3(pos[0], pos[1], pos[2]);
                tComp.transform.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                tComp.transform.scale = glm::vec3(scl[0], scl[1], scl[2]);
                tComp.transform.updatematrix = true;
            }

            // 3. Меш
            if (objJson.contains("mesh")) {
                auto& mComp = registry.emplace<MeshComponent>(entity);
                auto mJson = objJson["mesh"];
                if (mJson.contains("isStatic")) mComp.isStatic = mJson["isStatic"];
                if (mJson.contains("isVisible")) mComp.isVisible = mJson["isVisible"];
                if (mJson.contains("castShadow")) mComp.castShadow = mJson["castShadow"];
                if (mJson.contains("model_path")) mComp.modelPath = mJson["model_path"].get<std::string>();

                if (mJson.contains("materials")) {
                    for (const auto& matPath : mJson["materials"]) {
                        mComp.materialPaths.push_back(matPath.get<std::string>());
                    }
                }

                // Подгружаем саму модель
                if (!mComp.modelPath.empty()) {
                    std::string fullModelPath = basePath + "/" + mComp.modelPath;
                    if (loadedModels.find(fullModelPath) == loadedModels.end()) {
                        loadedModels[fullModelPath] = new Model(fullModelPath, basePath);
                    }
                    Model* model = loadedModels[fullModelPath];

                    if (model && !model->meshes.empty()) {
                        for (int i = 0; i < model->meshes.size(); i++) {
                            std::string relativeMatPath = (i < mComp.materialPaths.size()) ? mComp.materialPaths[i] : "";
                            Material* mat = nullptr;
                            if (!relativeMatPath.empty()) {
                                mat = LoadMaterial(basePath + "/" + relativeMatPath, basePath);
                            }
                            if (mat == nullptr) mat = new Material();
                            mComp.renderer.AddSubMesh(&model->meshes[i], mat);
                        }
                    }
                }
            }

            // 4. Свет
            if (objJson.contains("light")) {
                auto& lComp = registry.emplace<LightComponent>(entity).light;
                auto lJson = objJson["light"];
                if (lJson.contains("enable")) lComp.enable = lJson["enable"];
                if (lJson.contains("type")) lComp.type = (LightType)lJson["type"].get<int>();
                if (lJson.contains("mobility")) lComp.mobility = (LightMobility)lJson["mobility"].get<int>();
                if (lJson.contains("color")) {
                    auto c = lJson["color"];
                    lComp.color = glm::vec3(c[0], c[1], c[2]);
                }
                if (lJson.contains("intensity")) lComp.intensity = lJson["intensity"];
                if (lJson.contains("radius")) lComp.radius = lJson["radius"];
                if (lJson.contains("innerCone")) lComp.innerCone = lJson["innerCone"];
                if (lJson.contains("outerCone")) lComp.outerCone = lJson["outerCone"];
                if (lJson.contains("castShadows")) lComp.castShadows = lJson["castShadows"];
                lComp.needsShadowUpdate = true;
            }

            // 5. Физика
            if (objJson.contains("physics")) {
                auto& pComp = registry.emplace<PhysicsComponent>(entity);
                auto pJson = objJson["physics"];
                if (pJson.contains("colliderType")) pComp.colliderType = (ColliderType)pJson["colliderType"].get<int>();
                if (pJson.contains("bodyType")) pComp.bodyType = (RigidBodyType)pJson["bodyType"].get<int>();
                if (pJson.contains("extents")) {
                    auto ext = pJson["extents"];
                    pComp.extents = glm::vec3(ext[0], ext[1], ext[2]);
                }
                if (pJson.contains("radius")) pComp.radius = pJson["radius"];
                if (pJson.contains("mass")) pComp.mass = pJson["mass"];
                if (pJson.contains("friction")) pComp.friction = pJson["friction"];
                if (pJson.contains("restitution")) pComp.restitution = pJson["restitution"];

                pComp.rebuildPhysics = true; // Заставляем физический движок пересобрать тело!
            }
        }

        // ВТОРОЙ ПРОХОД: Восстановление Иерархии
        for (const auto& objJson : sceneJson["objects"]) {
            if (!objJson.contains("id") || !objJson.contains("hierarchy")) continue;

            uint32_t savedId = objJson["id"].get<uint32_t>();
            entt::entity childEntity = idMap[savedId];

            if (objJson["hierarchy"].contains("parent")) {
                uint32_t parentSavedId = objJson["hierarchy"]["parent"].get<uint32_t>();

                // Если родитель с таким ID был загружен
                if (idMap.find(parentSavedId) != idMap.end()) {
                    entt::entity parentEntity = idMap[parentSavedId];

                    // Назначаем родителя ребенку
                    auto& childHierarchy = registry.get_or_emplace<HierarchyComponent>(childEntity);
                    childHierarchy.parent = parentEntity;

                    // Добавляем ребенка в список детей родителя
                    auto& parentHierarchy = registry.get_or_emplace<HierarchyComponent>(parentEntity);
                    parentHierarchy.children.push_back(childEntity);
                }
            }
        }
    }
};
inline std::unordered_map<std::string, Model*> Serializer::loadedModels;
inline std::unordered_map<std::string, Material*> Serializer::loadedMaterials;