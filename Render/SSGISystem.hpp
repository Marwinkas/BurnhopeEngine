#pragma once

#include "../Utils/Device.hpp"
#include "ComputeShader.hpp"
#include "../Utils/Descriptors.hpp"
#include <memory>
#include <vector>

namespace burnhope {

class SSGISystem {
public:
    SSGISystem(
        BurnhopeDevice& device,
        VkDescriptorSetLayout globalSetLayout,
        VkDescriptorSetLayout gBufferLayout,
        VkDescriptorSetLayout shadowLayout,
        VkDescriptorSetLayout ssgiLayout
    );
    ~SSGISystem();

    SSGISystem(const SSGISystem&) = delete;
    SSGISystem& operator=(const SSGISystem&) = delete;

    void computeSSGI(
        VkCommandBuffer cmd,
        VkDescriptorSet globalSet,
        VkDescriptorSet gBufferSet,
        VkDescriptorSet shadowSet,
        VkDescriptorSet ssgiSet,
        uint32_t width,
        uint32_t height
    );
    
    void computeDenoise(
        VkCommandBuffer cmd,
        VkDescriptorSet globalSet,
        VkDescriptorSet gBufferSet,
        VkDescriptorSet ssgiSet,
        uint32_t width,
        uint32_t height
    );

private:
    BurnhopeDevice& lveDevice;
    std::unique_ptr<ComputeShader> ssgiShader;
    std::unique_ptr<ComputeShader> denoiseShader;
};

} // namespace burnhope