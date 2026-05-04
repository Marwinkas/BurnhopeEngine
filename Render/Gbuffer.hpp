#pragma once

#include "../Utils/Device.hpp"
#include "Texture.hpp"

#include <memory>
#include <vector>

namespace burnhope {

    class BurnhopeGBuffer {
    public:
        BurnhopeGBuffer(BurnhopeDevice& device, VkExtent2D windowExtent);
        ~BurnhopeGBuffer();

        
        BurnhopeGBuffer(const BurnhopeGBuffer&) = delete;
        BurnhopeGBuffer& operator=(const BurnhopeGBuffer&) = delete;

        VkRenderPass getRenderPass() const { return renderPass; }
        VkFramebuffer getFramebuffer() const { return framebuffer; }

        
        BurnhopeTexture* getNormalRoughness() { return normalRoughness.get(); }
        BurnhopeTexture* getAlbedoMetallic() { return albedoMetallic.get(); }
        BurnhopeTexture* getHeightAO() { return heightAO.get(); }
        BurnhopeTexture* getDepth() { return depthTexture.get(); }

    private:
        void createResources();
        void createRenderPass();
        void createFramebuffer();

        BurnhopeDevice& lveDevice;
        VkExtent2D extent;

        
        std::unique_ptr<BurnhopeTexture> normalRoughness;
        std::unique_ptr<BurnhopeTexture> albedoMetallic;
        std::unique_ptr<BurnhopeTexture> heightAO;
        std::unique_ptr<BurnhopeTexture> depthTexture;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

} 