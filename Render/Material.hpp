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
        
        // Добавляем память для путей к текстурам
        std::string albedoPath = "";
        std::string normalPath = "";
        std::string heightPath = "";
        std::string metallicPath = "";
        std::string roughnessPath = "";
        std::string aoPath = "";

        std::shared_ptr<BurnhopeTexture> albedoMap = nullptr;
        std::shared_ptr<BurnhopeTexture> normalMap = nullptr;
        std::shared_ptr<BurnhopeTexture> heightMap = nullptr;
        std::shared_ptr<BurnhopeTexture> metallicMap = nullptr;
        std::shared_ptr<BurnhopeTexture> roughnessMap = nullptr;
        std::shared_ptr<BurnhopeTexture> aoMap = nullptr;
        
        bool isORM = false;
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
            j["isORM"] = isORM;
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
            file >> j;

            auto mat = std::make_shared<Material>();
            
            if (j.contains("uvScale")) {
                mat->uvScale = glm::vec2(j["uvScale"][0], j["uvScale"][1]);
            }
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

            return mat;
        }
    };
}