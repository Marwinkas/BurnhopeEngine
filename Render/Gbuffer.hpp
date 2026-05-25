#pragma once
#include "../Utils/Device.hpp"
#include "Texture.hpp"
#include <memory>
#include <vector>
#include "GraphicsShader.hpp"
namespace burnhope
{
    class BurnhopeGBuffer
    {
    public:
        BurnhopeGBuffer(BurnhopeDevice &device, VkExtent2D windowExtent);
        ~BurnhopeGBuffer();
        BurnhopeGBuffer(const BurnhopeGBuffer &) = delete;
        BurnhopeGBuffer &operator=(const BurnhopeGBuffer &) = delete;
        BurnhopeTexture *getNormalRoughness() { return normalRoughness.get(); }
        BurnhopeTexture *getAlbedoMetallic() { return albedoMetallic.get(); }
        BurnhopeTexture *getHeightAO() { return heightAO.get(); }
        BurnhopeTexture *getEmissive() { return gEmissive.get(); }
        BurnhopeTexture *getPortalID() { return gPortalID.get(); }
        BurnhopeTexture *getDepth() { return depthTexture.get(); }

    private:
        void createResources();
        BurnhopeDevice &device_;
        VkExtent2D extent;
        std::unique_ptr<BurnhopeTexture> normalRoughness;
        std::unique_ptr<BurnhopeTexture> albedoMetallic;
        std::unique_ptr<BurnhopeTexture> heightAO;
        std::unique_ptr<BurnhopeTexture> gEmissive;
        std::unique_ptr<BurnhopeTexture> gPortalID;
        std::unique_ptr<BurnhopeTexture> depthTexture;
    };
}