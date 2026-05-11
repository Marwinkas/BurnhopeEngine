#pragma once
#include "Texture.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp> // Подключаем библиотеку для JSON

using json = nlohmann::json;

namespace burnhope
{
    class Material
    {
    public:
        int ID;
        glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);

        glm::vec3 albedoColor = glm::vec3(1.0f, 1.0f, 1.0f);
        glm::vec3 emissiveColor = glm::vec3(0.0f, 0.0f, 0.0f);
        
        float metallicStrength = 0.0f;
        float roughnessStrength = 1.0f;
        float normalStrength = 1.0f;
        float heightStrength = 1.0f;
        bool repeatTexture = true;
        bool useTriplanar = false;
        float triplanarScale = 1.0f;
        float aoStrength = 1.0f;
        // Добавляем память для путей к текстурам
        std::string emissivePath = "";
        std::string albedoPath = "";
        std::string normalPath = "";
        std::string heightPath = "";
        std::string metallicPath = "";
        std::string roughnessPath = "";
        std::string aoPath = "";

        std::shared_ptr<BurnhopeTexture> emissiveMap = nullptr;
        std::shared_ptr<BurnhopeTexture> albedoMap = nullptr;
        std::shared_ptr<BurnhopeTexture> normalMap = nullptr;
        std::shared_ptr<BurnhopeTexture> heightMap = nullptr;
        std::shared_ptr<BurnhopeTexture> metallicMap = nullptr;
        std::shared_ptr<BurnhopeTexture> roughnessMap = nullptr;
        std::shared_ptr<BurnhopeTexture> aoMap = nullptr;
        
        bool isORM = false;
        bool hasEmissive = false;
        float emissiveIntensity = 0.0f;
        bool hasAlbedo = false;
        bool hasNormal = false;
        bool hasHeight = false;
        bool hasMetallic = false;
        bool hasRoughness = false;
        bool hasAO = false;

        Material()
        {
            static int MaterialGlobalID = 0;
            ID = MaterialGlobalID++;
        }

        // Немного обновим методы, чтобы они запоминали путь
        void setEmissive(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            emissiveMap = tex;
            hasEmissive = (tex != nullptr);
            if (!path.empty()) emissivePath = path;
        }

        void setAlbedo(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            albedoMap = tex;
            hasAlbedo = (tex != nullptr);
            if (!path.empty()) albedoPath = path;
        }

        void setNormal(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            normalMap = tex;
            hasNormal = (tex != nullptr);
            if (!path.empty()) normalPath = path;
        }

        void setMetallic(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            metallicMap = tex;
            hasMetallic = (tex != nullptr);
            if (!path.empty()) metallicPath = path;
        }

        void setRoughness(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            roughnessMap = tex;
            hasRoughness = (tex != nullptr);
            if (!path.empty()) roughnessPath = path;
        }

        void setAO(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            aoMap = tex;
            hasAO = (tex != nullptr);
            if (!path.empty()) aoPath = path;
        }

        void setHeight(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            heightMap = tex;
            hasHeight = (tex != nullptr);
            if (!path.empty()) heightPath = path;
        }

        std::shared_ptr<BurnhopeTexture> getAlbedoSafe(std::shared_ptr<BurnhopeTexture> defaultWhite)
        {
            return hasAlbedo ? albedoMap : defaultWhite;
        }

        std::shared_ptr<BurnhopeTexture> getNormalSafe(std::shared_ptr<BurnhopeTexture> defaultNormal)
        {
            return hasNormal ? normalMap : defaultNormal;
        }

        // Записываем материал в красивый JSON файлик
        void saveToJson(const std::string& filePath)
        {
            json j;
            j["uvScale"] = {uvScale.x, uvScale.y};
            j["emissiveIntensity"] = emissiveIntensity;
            j["emissiveColor"] = {emissiveColor.x, emissiveColor.y, emissiveColor.z};
            j["emissivePath"] = emissivePath;
            j["isORM"] = isORM;
            j["albedoColor"] = {albedoColor.x, albedoColor.y, albedoColor.z};
            j["metallicStrength"] = metallicStrength;
            j["roughnessStrength"] = roughnessStrength;
            j["normalStrength"] = normalStrength;
            j["heightStrength"] = heightStrength;
            j["aoStrength"] = aoStrength;
            j["repeatTexture"] = repeatTexture;
            j["useTriplanar"] = useTriplanar;
            j["triplanarScale"] = triplanarScale;
            j["albedoPath"] = albedoPath;
            j["normalPath"] = normalPath;
            j["heightPath"] = heightPath;
            j["metallicPath"] = metallicPath;
            j["roughnessPath"] = roughnessPath;
            j["aoPath"] = aoPath;

            std::ofstream file(filePath);
            if (file.is_open()) {
                file << j.dump(4); // Цифра 4 делает текст красивым, с отступами
            }
        }

        // Читаем материал из файлика и сразу загружаем текстуры
        static std::shared_ptr<Material> loadFromJson(BurnhopeDevice& device, const std::string& filePath)
        {
            std::ifstream file(filePath);
            if (!file.is_open()) return nullptr;

            json j;
            try { file >> j; } catch(...) { return nullptr; }
            if (!j.is_object()) return nullptr;

            auto mat = std::make_shared<Material>();
            
            if (j.contains("albedoColor")) mat->albedoColor = glm::vec3(j["albedoColor"][0], j["albedoColor"][1], j["albedoColor"][2]);
            if (j.contains("emissiveColor")) mat->emissiveColor = glm::vec3(j["emissiveColor"][0], j["emissiveColor"][1], j["emissiveColor"][2]);
            if (j.contains("metallicStrength")) mat->metallicStrength = j["metallicStrength"];
            if (j.contains("roughnessStrength")) mat->roughnessStrength = j["roughnessStrength"];
            if (j.contains("normalStrength")) mat->normalStrength = j["normalStrength"];
            if (j.contains("heightStrength")) mat->heightStrength = j["heightStrength"];
            if (j.contains("aoStrength")) mat->aoStrength = j["aoStrength"];
            else if (j.contains("aoFactor")) mat->aoStrength = j["aoFactor"];
            if (j.contains("repeatTexture")) mat->repeatTexture = j["repeatTexture"];
            if (j.contains("useTriplanar")) mat->useTriplanar = j["useTriplanar"];
            if (j.contains("triplanarScale")) mat->triplanarScale = j["triplanarScale"];

            if (j.contains("uvScale")) {
                mat->uvScale = glm::vec2(j["uvScale"][0], j["uvScale"][1]);
            }
            if (j.contains("emissiveIntensity")) mat->emissiveIntensity = j["emissiveIntensity"];
            
            if (j.contains("isORM")) mat->isORM = j["isORM"];

            // Аккуратно достаем пути. Если они есть — загружаем картинку!
            std::string aPath = j.value("albedoPath", "");
            if (!aPath.empty()) mat->setAlbedo(BurnhopeTexture::createTextureFromFile(device, aPath), aPath);

            std::string nPath = j.value("normalPath", "");
            if (!nPath.empty()) mat->setNormal(BurnhopeTexture::createDataTextureFromFile(device, nPath), nPath);

            std::string rPath = j.value("roughnessPath", "");
            if (!rPath.empty()) mat->setRoughness(BurnhopeTexture::createDataTextureFromFile(device, rPath), rPath);

            std::string mPath = j.value("metallicPath", "");
            if (!mPath.empty()) mat->setMetallic(BurnhopeTexture::createDataTextureFromFile(device, mPath), mPath);

            std::string aoPath = j.value("aoPath", "");
            if (!aoPath.empty()) mat->setAO(BurnhopeTexture::createDataTextureFromFile(device, aoPath), aoPath);

            std::string hPath = j.value("heightPath", "");
            if (!hPath.empty()) mat->setHeight(BurnhopeTexture::createDataTextureFromFile(device, hPath), hPath);

            std::string ePath = j.value("emissivePath", "");
            if (!ePath.empty()) mat->setEmissive(BurnhopeTexture::createTextureFromFile(device, ePath), ePath);

            return mat;
        }
    };
}