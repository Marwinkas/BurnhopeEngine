

#ifndef BURNHOPE_MATERIAL_HPP
#define BURNHOPE_MATERIAL_HPP
#include "Texture.hpp"
#include "../Utils/AssetPool.hpp"
#include "../Utils/DirectXMathCompat.hpp"
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
        float2 uvScale = float2{1.0f, 1.0f};

        float4 albedoColor = float4{1.0f, 1.0f, 1.0f, 1.0f};
        float3 emissiveColor = float3{0.0f, 0.0f, 0.0f};
        
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

        TextureHandle emissiveMap{kInvalidTextureHandle};
        TextureHandle albedoMap{kInvalidTextureHandle};
        TextureHandle normalMap{kInvalidTextureHandle};
        TextureHandle heightMap{kInvalidTextureHandle};
        TextureHandle metallicMap{kInvalidTextureHandle};
        TextureHandle roughnessMap{kInvalidTextureHandle};
        TextureHandle aoMap{kInvalidTextureHandle};
        TextureHandle ormMap{kInvalidTextureHandle};
        TextureHandle alphaMap{kInvalidTextureHandle};
        TextureHandle lightMaskMap{kInvalidTextureHandle};
        TextureHandle rimMaskMap{kInvalidTextureHandle};

        TextureHandle packedAlbedoAlpha{kInvalidTextureHandle};
        TextureHandle packedNormal{kInvalidTextureHandle};
        TextureHandle packedORMX{kInvalidTextureHandle};
        TextureHandle packedEmissive{kInvalidTextureHandle};

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
        static TextureHandle registerTexture(
            TexturePool& pool,
            BindlessRegistry& bindless,
            std::unique_ptr<BurnhopeTexture> tex) {
          if (!tex) {
            return kInvalidTextureHandle;
          }
          return pool.emplace(std::move(tex), bindless);
        }

        void setEmissive(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                         const std::string& path = "") {
          emissiveMap = registerTexture(pool, bindless, std::move(tex));
          hasEmissive = emissiveMap != kInvalidTextureHandle;
          emissivePath = path;
        }

        void setAlbedo(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                       const std::string& path = "") {
          albedoMap = registerTexture(pool, bindless, std::move(tex));
          hasAlbedo = albedoMap != kInvalidTextureHandle;
          albedoPath = path;
        }

        void setNormal(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                       const std::string& path = "") {
          normalMap = registerTexture(pool, bindless, std::move(tex));
          hasNormal = normalMap != kInvalidTextureHandle;
          normalPath = path;
        }

        void setMetallic(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                         const std::string& path = "") {
          metallicMap = registerTexture(pool, bindless, std::move(tex));
          hasMetallic = metallicMap != kInvalidTextureHandle;
          metallicPath = path;
        }

        void setRoughness(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                          const std::string& path = "") {
          roughnessMap = registerTexture(pool, bindless, std::move(tex));
          hasRoughness = roughnessMap != kInvalidTextureHandle;
          roughnessPath = path;
        }

        void setAO(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                   const std::string& path = "") {
          aoMap = registerTexture(pool, bindless, std::move(tex));
          hasAO = aoMap != kInvalidTextureHandle;
          aoPath = path;
        }

        void setHeight(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                       const std::string& path = "") {
          heightMap = registerTexture(pool, bindless, std::move(tex));
          hasHeight = heightMap != kInvalidTextureHandle;
          heightPath = path;
        }

        void setORM(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                    const std::string& path = "") {
          ormMap = registerTexture(pool, bindless, std::move(tex));
          hasORM = ormMap != kInvalidTextureHandle;
          ormPath = path;
        }

        void setAlpha(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                      const std::string& path = "") {
          alphaMap = registerTexture(pool, bindless, std::move(tex));
          hasAlpha = alphaMap != kInvalidTextureHandle;
          alphaPath = path;
        }

        void setLightMask(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                          const std::string& path = "") {
          lightMaskMap = registerTexture(pool, bindless, std::move(tex));
          hasLightMask = lightMaskMap != kInvalidTextureHandle;
          lightMaskPath = path;
        }

        void setRimMask(TexturePool& pool, BindlessRegistry& bindless, std::unique_ptr<BurnhopeTexture> tex,
                        const std::string& path = "") {
          rimMaskMap = registerTexture(pool, bindless, std::move(tex));
          hasRimMask = rimMaskMap != kInvalidTextureHandle;
          rimMaskPath = path;
        }

        [[nodiscard]] TextureHandle getAlbedoHandle(TextureHandle defaultWhite) const {
          return hasAlbedo ? albedoMap : defaultWhite;
        }

        [[nodiscard]] TextureHandle getNormalHandle(TextureHandle defaultNormal) const {
          return hasNormal ? normalMap : defaultNormal;
        }

        /** Editor ImGui preview only (non-owning). */
        [[nodiscard]] std::shared_ptr<BurnhopeTexture> albedoMapForEditor(TexturePool& pool) const {
          if (auto* t = pool.resolve(albedoMap)) {
            return std::shared_ptr<BurnhopeTexture>(t, [](BurnhopeTexture*) {});
          }
          return nullptr;
        }
        [[nodiscard]] std::shared_ptr<BurnhopeTexture> normalMapForEditor(TexturePool& pool) const {
          if (auto* t = pool.resolve(normalMap)) {
            return std::shared_ptr<BurnhopeTexture>(t, [](BurnhopeTexture*) {});
          }
          return nullptr;
        }
        [[nodiscard]] std::shared_ptr<BurnhopeTexture> packedAlbedoForEditor(TexturePool& pool) const {
          if (auto* t = pool.resolve(packedAlbedoAlpha)) {
            return std::shared_ptr<BurnhopeTexture>(t, [](BurnhopeTexture*) {});
          }
          return nullptr;
        }

        static std::shared_ptr<Material> loadFromJson(
            BurnhopeDevice& device,
            TexturePool& pool,
            BindlessRegistry& bindless,
            const std::string& filePath);

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

        void packTextures(
            BurnhopeDevice& device,
            TexturePool& pool,
            BindlessRegistry& bindless,
            std::vector<std::unique_ptr<BurnhopeTexture>>& safeDeleteQueue) {
            std::string cacheDir = "cache/";
            if (!std::filesystem::exists(cacheDir)) std::filesystem::create_directory(cacheDir);

            auto getHash = [](const std::string& s1, const std::string& s2 = "", const std::string& s3 = "", const std::string& s4 = "") {
                return std::to_string(std::hash<std::string>{}(s1 + "|" + s2 + "|" + s3 + "|" + s4));
            };

            auto replacePacked = [&](TextureHandle& slot, const std::string& outPath) {
              if (slot != kInvalidTextureHandle) {
                if (auto old = pool.release(slot)) {
                  safeDeleteQueue.push_back(std::move(old));
                }
                slot = kInvalidTextureHandle;
              }
              if (!outPath.empty()) {
                slot = registerTexture(
                    pool, bindless,
                    std::make_unique<BurnhopeTexture>(device, outPath));
              }
            };

            if (!albedoPath.empty() || !alphaPath.empty()) {
                std::string out = cacheDir + "pack_rgba_" + getHash(albedoPath, alphaPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packAlbedoAlpha(albedoPath, alphaPath, out);
                replacePacked(packedAlbedoAlpha, out);
            } else {
                replacePacked(packedAlbedoAlpha, "");
            }

            if (!normalPath.empty()) {
                std::string out = cacheDir + "pack_norm_" + getHash(normalPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packNormal(normalPath, out);
                replacePacked(packedNormal, out);
            } else {
                replacePacked(packedNormal, "");
            }

            if (!aoPath.empty() || !roughnessPath.empty() || !metallicPath.empty() || !heightPath.empty()) {
                std::string out = cacheDir + "pack_ormx_" + getHash(aoPath, roughnessPath, metallicPath, heightPath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packORMX(aoPath, roughnessPath, metallicPath, heightPath, out);
                replacePacked(packedORMX, out);
                hasORM = true;
            } else {
                replacePacked(packedORMX, "");
            }

            if (!emissivePath.empty()) {
                std::string out = cacheDir + "pack_emis_" + getHash(emissivePath) + ".bhtex";
                if (!std::filesystem::exists(out)) BurnhopeTexture::packEmissive(emissivePath, out);
                replacePacked(packedEmissive, out);
            } else {
                replacePacked(packedEmissive, "");
            }
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

    };
}
#endif