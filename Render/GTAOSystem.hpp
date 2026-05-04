#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "ComputeShader.hpp"
#include "iostream"
#include <memory>
namespace burnhope
{
    class GTAOSystem
    {
    public:
        GTAOSystem(BurnhopeDevice &device, const std::vector<VkDescriptorSetLayout> &layouts);
        void compute(VkCommandBuffer cmd, VkDescriptorSet globalSet, VkDescriptorSet gtaoSet, uint32_t width, uint32_t height);
        ~GTAOSystem();

    private:
        BurnhopeDevice &lveDevice;
        std::unique_ptr<ComputeShader> shader;
    };
}