#include "CullingSystem.hpp"
#include "ComputeShader.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <stdexcept>
#include <fstream>
#include <array>
namespace burnhope
{
    CullingSystem::CullingSystem(BurnhopeDevice &device, uint32_t maxObjects)
        : device(device), maxObjects(maxObjects)
    {
        drawCommandBuffer = std::make_unique<BurnhopeBuffer>(
            device, sizeof(VkDrawIndexedIndirectCommand), maxObjects,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        subMeshBuffer = std::make_unique<BurnhopeBuffer>(
            device, sizeof(SubMeshGPUInfo), maxObjects,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        cullLayout = BurnhopeDescriptorSetLayout::Builder(device)
                         .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
                         .build();
        cullPool = BurnhopeDescriptorPool::Builder(device)
                       .setMaxSets(4)
                       .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)
                       .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12)
                       .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4)
                       .build();
        createPipeline();
    }
    void CullingSystem::bindObjectBuffer(VkBuffer objectBuf, VkDeviceSize size)
    {
        externalObjectBuffer = objectBuf;
        externalObjectBufferSize = size;
    }
    void CullingSystem::updateHiZDescriptor(VkDescriptorImageInfo hiZInfo)
    {
        if (cullSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {cullSet};
            cullPool->freeDescriptors(toFree);
        }
        VkDescriptorBufferInfo objInfo{externalObjectBuffer, 0, externalObjectBufferSize};
        auto subInfo = subMeshBuffer->descriptorInfo();
        auto drawInfo = drawCommandBuffer->descriptorInfo();
        BurnhopeDescriptorWriter(*cullLayout, *cullPool)
            .writeBuffer(0, &objInfo)
            .writeBuffer(1, &subInfo)
            .writeBuffer(2, &drawInfo)
            .writeImage(3, &hiZInfo)
            .build(cullSet);
    }
    CullingSystem::~CullingSystem()
    {
        if (cullSet != VK_NULL_HANDLE)
        {
            std::vector<VkDescriptorSet> toFree = {cullSet};
            cullPool->freeDescriptors(toFree);
        }
    }
    void CullingSystem::uploadSubMeshData(const std::vector<SubMeshGPUInfo> &data)
    {
        BurnhopeBuffer staging(device, sizeof(SubMeshGPUInfo), data.size(),
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.map();
        staging.writeToBuffer((void *)data.data());
        device.copyBuffer(staging.getBuffer(), subMeshBuffer->getBuffer(),
                          sizeof(SubMeshGPUInfo) * data.size());
    }
    void CullingSystem::createPipeline()
    {
        cullShader = std::make_unique<ComputeShader>(
            device,
            "shaders/culling.comp.spv",
            std::vector<VkDescriptorSetLayout>{cullLayout->getDescriptorSetLayout()},
            sizeof(CullPushConstants));
    }
    void CullingSystem::dispatchCulling(VkCommandBuffer cmd,
                                        const float4x4 &viewProj,
                                        const float3 &camPos,
                                        const std::array<float4, 6> &planes,
                                        uint32_t objectCount)
    {
        VkBufferMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.buffer = drawCommandBuffer->getBuffer();
        barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &barrier, 0, nullptr);
        cullShader->bind(cmd);
        cullShader->bindDescriptorSets(cmd, {cullSet});
        CullPushConstants push{};
        push.viewProj = viewProj;
        for (int i = 0; i < 6; i++)
            push.frustumPlanes[i] = planes[i];
        push.camPos = camPos;
        push.objectCount = objectCount;
        push.zNear = 0.01f;
        cullShader->pushConstants(cmd, &push, sizeof(push));
        uint32_t groups = (objectCount + 63) / 64;
        cullShader->dispatch(cmd, groups, 1, 1);
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 0, nullptr, 1, &barrier, 0, nullptr);
    }
    std::array<float4, 6> CullingSystem::extractFrustumPlanes(const float4x4 &vp)
    {
        std::array<float4, 6> planes;
        float4 r0{vp._11, vp._21, vp._31, vp._41};
        float4 r1{vp._12, vp._22, vp._32, vp._42};
        float4 r2{vp._13, vp._23, vp._33, vp._43};
        float4 r3{vp._14, vp._24, vp._34, vp._44};
        planes[0] = r3 + r0;
        planes[1] = r3 - r0;
        planes[2] = r3 + r1;
        planes[3] = r3 - r1;
        planes[4] = r3 + r2;
        planes[5] = r3 - r2;
        for (int i = 0; i < 6; i++)
        {
            float3 temp{planes[i].x, planes[i].y, planes[i].z};
            float len = DirectX::XMVectorGetX(DirectX::XMVector3Length(DirectX::XMLoadFloat3(&temp)));
            planes[i] = planes[i] / len;
        }
        return planes;
    }
}