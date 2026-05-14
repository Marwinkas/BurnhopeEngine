#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Buffer.hpp"
#include "../Utils/Descriptors.hpp"
#include "ComputeShader.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
namespace burnhope
{
    struct SubMeshGPUInfo
    {
        glm::vec3 aabbMin;
        float boundingRadius;
        glm::vec3 aabbMax;
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
        glm::mat4 viewProj;
        glm::vec4 frustumPlanes[6];
        glm::vec3 camPos;
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
                             const glm::mat4 &viewProj,
                             const glm::vec3 &camPos,
                             const std::array<glm::vec4, 6> &frustumPlanes,
                             uint32_t objectCount);
        VkBuffer getDrawCommandBuffer() const
        {
            return drawCommandBuffer->getBuffer();
        }
        uint32_t getMaxDrawCount() const { return maxObjects; }
        static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &vp);

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