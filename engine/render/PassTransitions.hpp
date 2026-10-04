#pragma once

#include "rhi/Barrier.hpp"

namespace burnhope {

// Цвет или глубина после записи аттачмента становятся выборкой compute.
inline void transitionAttachmentToSampled(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect) {
    if ((aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0) {
        rhiImageBarrier(cmd, image,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT);
        return;
    }
    rhiImageBarrier(cmd, image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        aspect == 0 ? static_cast<VkImageAspectFlags>(VK_IMAGE_ASPECT_COLOR_BIT) : aspect);
}

// HDR в GENERAL под запись compute. По умолчанию картинка ещё не была читана.
inline void transitionHdrToStorageWrite(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE,
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE) {
    rhiImageBarrier(cmd, image,
        oldLayout, VK_IMAGE_LAYOUT_GENERAL,
        srcStage, srcAccess,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

// Запись storage становится выборкой. extraDstAccess дописывается к SAMPLED_READ.
inline void transitionHdrStorageToSampled(
    VkCommandBuffer cmd,
    VkImage image,
    VkAccessFlags2 extraDstAccess = 0,
    VkPipelineStageFlags2 dstStages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_GENERAL,
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_GENERAL,
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    VkAccessFlags2 srcAccess = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    rhiImageBarrier(cmd, image,
        oldLayout, newLayout,
        srcStage, srcAccess,
        dstStages, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | extraDstAccess);
}

// Выборка или пустая картинка становятся цветовым аттачментом.
inline void transitionToColorAttachment(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE,
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE,
    VkAccessFlags2 dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT) {
    rhiImageBarrier(cmd, image,
        oldLayout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        srcStage, srcAccess,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, dstAccess);
}

// То же для глубины. Стадия назначения — ранний и поздний тест.
inline void transitionToDepthAttachment(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE,
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE,
    VkAccessFlags2 dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT) {
    rhiImageBarrier(cmd, image,
        oldLayout, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        srcStage, srcAccess,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        dstAccess, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// Чтение HDR обратно в запись compute. Вход — COMPUTE, не NONE: предыдущий шейдер уже читал.
inline void transitionHdrSampledToStorageWrite(VkCommandBuffer cmd, VkImage image) {
    rhiImageBarrier(cmd, image,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

} // namespace burnhope
