#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Descriptors.hpp"
#include "ComputeShader.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <memory>
#include <vector>
namespace burnhope
{
    struct SubMeshGPUInfo
    {
        float3 aabbMin;
        float boundingRadius;
        float3 aabbMax;
        uint32_t lodCount;
        uint32_t indexCounts[4];
        uint32_t firstIndices[4];
        uint32_t materialIndex;
        uint32_t vrsRate;
        uint32_t pad2;
        uint32_t pad3;
    };
    struct CullPushConstants
    {
        float4x4 viewProj;
        float4 frustumPlanes[6];
        float3 camPos;
        uint32_t objectCount;
        float zNear;
        float pad[2];
    };
    class CullingSystem
    {
    public:
        CullingSystem(BurnhopeDevice &device, uint32_t maxObjects);
        ~CullingSystem();
        void bindObjectBuffer(VkBuffer objectBuf, VkDeviceSize objectBufSize);
        void uploadSubMeshData(const std::vector<SubMeshGPUInfo> &subMeshes);
        void updateHiZDescriptor(VkDescriptorImageInfo hiZInfo);
        void dispatchCulling(VkCommandBuffer cmd,
                             const float4x4 &viewProj,
                             const float3 &camPos,
                             const std::array<float4, 6> &frustumPlanes,
                             uint32_t objectCount);
        VkBuffer getDrawCommandBuffer() const
        {
            return drawCommandBuffer->getBuffer();
        }
        uint32_t getMaxDrawCount() const { return maxObjects; }
        static std::array<float4, 6> extractFrustumPlanes(const float4x4 &vp);

    private:
        void createPipeline();
        void createDescriptors();
        VkBuffer externalObjectBuffer = VK_NULL_HANDLE;
        VkDeviceSize externalObjectBufferSize = 0;
        BurnhopeDevice &device;
        uint32_t maxObjects;
        std::unique_ptr<BurnhopeBuffer> subMeshBuffer;
        std::unique_ptr<BurnhopeBuffer> drawCommandBuffer;
        std::unique_ptr<BurnhopeDescriptorSetLayout> cullLayout;
        std::unique_ptr<BurnhopeDescriptorPool> cullPool;
        VkDescriptorSet cullSet = VK_NULL_HANDLE;
        std::unique_ptr<ComputeShader> cullShader;
    };
}