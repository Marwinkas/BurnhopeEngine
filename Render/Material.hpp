#pragma once

#include "Texture.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace burnhope {

    class Material {
    public:
        int ID;
        glm::vec2 uvScale = glm::vec2(1.0f, 1.0f);

        
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

        Material() {
            static int MaterialGlobalID = 0;
            ID = MaterialGlobalID++;
        }

        
        
        void setAlbedo(std::shared_ptr<BurnhopeTexture> tex) {
            albedoMap = tex;
            hasAlbedo = (tex != nullptr);
        }

        void setNormal(std::shared_ptr<BurnhopeTexture> tex) {
            normalMap = tex;
            hasNormal = (tex != nullptr);
        }

        void setMetallic(std::shared_ptr<BurnhopeTexture> tex) {
            metallicMap = tex;
            hasMetallic = (tex != nullptr);
        }

        void setRoughness(std::shared_ptr<BurnhopeTexture> tex) {
            roughnessMap = tex;
            hasRoughness = (tex != nullptr);
        }

        void setAO(std::shared_ptr<BurnhopeTexture> tex) {
            aoMap = tex;
            hasAO = (tex != nullptr);
        }
        void setHeight(std::shared_ptr<BurnhopeTexture> tex) {
            heightMap = tex;
            hasHeight = (tex != nullptr);
        }
        
        
        std::shared_ptr<BurnhopeTexture> getAlbedoSafe(std::shared_ptr<BurnhopeTexture> defaultWhite) {
            return hasAlbedo ? albedoMap : defaultWhite;
        }

        std::shared_ptr<BurnhopeTexture> getNormalSafe(std::shared_ptr<BurnhopeTexture> defaultNormal) {
            return hasNormal ? normalMap : defaultNormal;
        }
    };

} 