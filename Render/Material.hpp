

#ifndef BURNHOPE_MATERIAL_HPP
#define BURNHOPE_MATERIAL_HPP
#include "Texture.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp> // Подключаем библиотеку для JSON
#include <thread>

#include <memory>
using json = nlohmann::json;

namespace burnhope
{ 
    class Material: public std::enable_shared_from_this<Material>
    {
    public:
        int ID;
        glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);

        glm::vec4 albedoColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
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
        std::string ormPath = "";
        std::string alphaPath = "";
        std::string lightMaskPath = "";
        std::string rimMaskPath = "";

        std::shared_ptr<BurnhopeTexture> emissiveMap = nullptr;
        std::shared_ptr<BurnhopeTexture> albedoMap = nullptr;
        std::shared_ptr<BurnhopeTexture> normalMap = nullptr;
        std::shared_ptr<BurnhopeTexture> heightMap = nullptr;
        std::shared_ptr<BurnhopeTexture> metallicMap = nullptr;
        std::shared_ptr<BurnhopeTexture> roughnessMap = nullptr;
        std::shared_ptr<BurnhopeTexture> aoMap = nullptr;
        std::shared_ptr<BurnhopeTexture> ormMap = nullptr;
        std::shared_ptr<BurnhopeTexture> alphaMap = nullptr;
        std::shared_ptr<BurnhopeTexture> lightMaskMap = nullptr;
        std::shared_ptr<BurnhopeTexture> rimMaskMap = nullptr;
        
        std::shared_ptr<BurnhopeTexture> packedAlbedoAlpha = nullptr;
        std::shared_ptr<BurnhopeTexture> packedNormal = nullptr;
        std::shared_ptr<BurnhopeTexture> packedORMX = nullptr;
        std::shared_ptr<BurnhopeTexture> packedEmissive = nullptr;

        bool hasEmissive = false;
        float emissiveIntensity = 0.0f;
        bool hasAlbedo = false;
        bool hasNormal = false;
        bool hasHeight = false;
        bool hasMetallic = false;
        bool hasRoughness = false;
        bool hasAO = false;
        bool hasORM = false;
        bool hasAlpha = false;
        bool hasLightMask = false;
        bool hasRimMask = false;
        bool isTransparent = false;

        std::atomic<bool> isPacking{false};
        std::atomic<bool> pendingReload{false};
        std::atomic<bool> needsAnotherPack{false};

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
            emissivePath = path;
        }

        void setAlbedo(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            albedoMap = tex;
            hasAlbedo = (tex != nullptr);
            albedoPath = path;
        }

        void setNormal(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            normalMap = tex;
            hasNormal = (tex != nullptr);
            normalPath = path;
        }

        void setMetallic(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            metallicMap = tex;
            hasMetallic = (tex != nullptr);
            metallicPath = path;
        }

        void setRoughness(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            roughnessMap = tex;
            hasRoughness = (tex != nullptr);
            roughnessPath = path;
        }

        void setAO(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            aoMap = tex;
            hasAO = (tex != nullptr);
            aoPath = path;
        }

        void setHeight(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "")
        {
            heightMap = tex;
            hasHeight = (tex != nullptr);
            heightPath = path;
        }
        void setORM(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "") {
            ormMap = tex;
            hasORM = (tex != nullptr);
            ormPath = path;
        }
        void setAlpha(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "") {
            alphaMap = tex;
            hasAlpha = (tex != nullptr);
            alphaPath = path;
        }
        void setLightMask(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "") {
            lightMaskMap = tex;
            hasLightMask = (tex != nullptr);
            lightMaskPath = path;
        }
        void setRimMask(std::shared_ptr<BurnhopeTexture> tex, const std::string& path = "") {
            rimMaskMap = tex;
            hasRimMask = (tex != nullptr);
            rimMaskPath = path;
        }

        std::shared_ptr<BurnhopeTexture> getAlbedoSafe(std::shared_ptr<BurnhopeTexture> defaultWhite)
        {
            return hasAlbedo ? albedoMap : defaultWhite;
        }

        std::shared_ptr<BurnhopeTexture> getNormalSafe(std::shared_ptr<BurnhopeTexture> defaultNormal)
        {
            return hasNormal ? normalMap : defaultNormal;
        }

        void packTexturesAsync() {
            if (isPacking) {
                needsAnotherPack = true;
                return;
            }
            isPacking = true;
            needsAnotherPack = false;

            std::string alb = albedoPath, alp = alphaPath, nrm = normalPath;
            std::string ao = aoPath, rgh = roughnessPath, met = metallicPath, hgt = heightPath;
            std::string emi = emissivePath;

            std::thread([self = shared_from_this(), alb, alp, nrm, ao, rgh, met, hgt, emi]() {
                std::string cacheDir = "cache/";
                if (!std::filesystem::exists(cacheDir)) std::filesystem::create_directory(cacheDir);

                auto getHash = [](const std::string& s1, const std::string& s2 = "", const std::string& s3 = "", const std::string& s4 = "") {
                    return std::to_string(std::hash<std::string>{}(s1 + "|" + s2 + "|" + s3 + "|" + s4));
                };

                if (!alb.empty() || !alp.empty()) {
                    std::string out = cacheDir + "pack_rgba_" + getHash(alb, alp) + ".bhtex";
                    if (!std::filesystem::exists(out)) BurnhopeTexture::packAlbedoAlpha(alb, alp, out);
                }
                if (!nrm.empty()) {
                    std::string out = cacheDir + "pack_norm_" + getHash(nrm) + ".bhtex";
                    if (!std::filesystem::exists(out)) BurnhopeTexture::packNormal(nrm, out);
                }
                if (!ao.empty() || !rgh.empty() || !met.empty() || !hgt.empty()) {
                    std::string out = cacheDir + "pack_ormx_" + getHash(ao, rgh, met, hgt) + ".bhtex";
                    if (!std::filesystem::exists(out)) BurnhopeTexture::packORMX(ao, rgh, met, hgt, out);
                }
                if (!emi.empty()) {
                    std::string out = cacheDir + "pack_emis_" + getHash(emi) + ".bhtex";
                    if (!std::filesystem::exists(out)) BurnhopeTexture::packEmissive(emi, out);
                }
                
                self->pendingReload = true;
                self->isPacking = false;
            }).detach();
        }

        void packTextures(BurnhopeDevice& device, std::vector<std::shared_ptr<void>>& safeDeleteQueue) {
            std::string cacheDir = "cache/";
            if (!std::filesystem::exists(cacheDir)) std::filesystem::create_directory(cacheDir);

            auto getHash = [](const std::string& s1, const std::string& s2 = "", const std::string& s3 = "", const std::string& s4 = "") {
                return std::to_string(std::hash<std::string>{}(s1 + "|" + s2 + "|" + s3 + "|" + s4));
            };

            if (!albedoPath.empty() || !alphaPath.empty()) {
                std::string out = cacheDir + "pack_rgba_" + getHash(albedoPath, alphaPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packAlbedoAlpha(albedoPath, alphaPath, out);
                if (packedAlbedoAlpha) safeDeleteQueue.push_back(packedAlbedoAlpha);
                packedAlbedoAlpha = BurnhopeTexture::createTextureFromFile(device, out);
            } else { if (packedAlbedoAlpha) safeDeleteQueue.push_back(packedAlbedoAlpha); packedAlbedoAlpha = nullptr; }

            if (!normalPath.empty()) {
                std::string out = cacheDir + "pack_norm_" + getHash(normalPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packNormal(normalPath, out);
                if (packedNormal) safeDeleteQueue.push_back(packedNormal);
                packedNormal = BurnhopeTexture::createTextureFromFile(device, out);
            } else { if (packedNormal) safeDeleteQueue.push_back(packedNormal); packedNormal = nullptr; }

            if (!aoPath.empty() || !roughnessPath.empty() || !metallicPath.empty() || !heightPath.empty()) {
                std::string out = cacheDir + "pack_ormx_" + getHash(aoPath, roughnessPath, metallicPath, heightPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packORMX(aoPath, roughnessPath, metallicPath, heightPath, out);
                if (packedORMX) safeDeleteQueue.push_back(packedORMX);
                packedORMX = BurnhopeTexture::createTextureFromFile(device, out);
                hasORM = true; // Принудительно используем запакованный вариант
            } else { if (packedORMX) safeDeleteQueue.push_back(packedORMX); packedORMX = nullptr; }

            if (!emissivePath.empty()) {
                std::string out = cacheDir + "pack_emis_" + getHash(emissivePath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packEmissive(emissivePath, out);
                if (packedEmissive) safeDeleteQueue.push_back(packedEmissive);
                packedEmissive = BurnhopeTexture::createTextureFromFile(device, out);
            } else { if (packedEmissive) safeDeleteQueue.push_back(packedEmissive); packedEmissive = nullptr; }
        }

        // Записываем материал в красивый JSON файлик
        void saveToJson(const std::string& filePath)
        {
            json j;
            j["uvScale"] = {uvScale.x, uvScale.y};
            j["emissiveIntensity"] = emissiveIntensity;
            j["emissiveColor"] = {emissiveColor.x, emissiveColor.y, emissiveColor.z};
            j["emissivePath"] = emissivePath;
            j["albedoColor"] = {albedoColor.x, albedoColor.y, albedoColor.z, albedoColor.w};
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
            j["ormPath"] = ormPath;
            j["alphaPath"] = alphaPath;
            j["lightMaskPath"] = lightMaskPath;
            j["rimMaskPath"] = rimMaskPath;
            j["isTransparent"] = isTransparent;

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
            
            if (j.contains("albedoColor")) {
                if (j["albedoColor"].size() == 4)
                    mat->albedoColor = glm::vec4(j["albedoColor"][0], j["albedoColor"][1], j["albedoColor"][2], j["albedoColor"][3]);
                else
                    mat->albedoColor = glm::vec4(j["albedoColor"][0], j["albedoColor"][1], j["albedoColor"][2], 1.0f);
            }
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
            if (j.contains("isTransparent")) mat->isTransparent = j["isTransparent"];

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
            
            std::string oPath = j.value("ormPath", "");
            if (!oPath.empty()) mat->setORM(BurnhopeTexture::createDataTextureFromFile(device, oPath), oPath);
            
            std::string alphaP = j.value("alphaPath", "");
            if (!alphaP.empty()) mat->setAlpha(BurnhopeTexture::createDataTextureFromFile(device, alphaP), alphaP);

            std::string lmPath = j.value("lightMaskPath", "");
            if (!lmPath.empty()) mat->setLightMask(BurnhopeTexture::createDataTextureFromFile(device, lmPath), lmPath);
            
            std::string rmPath = j.value("rimMaskPath", "");
            if (!rmPath.empty()) mat->setRimMask(BurnhopeTexture::createDataTextureFromFile(device, rmPath), rmPath);

            // Запускаем асинхронную упаковку при загрузке
            mat->packTexturesAsync();

            return mat;
        }
    };
}
#endif