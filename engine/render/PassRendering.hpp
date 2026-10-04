#pragma once

#include <volk.h>

namespace burnhope {

// Аттачменты живут в этом объекте, пока идёт проход. Деструктор закрывает рендеринг.
struct RenderingPass {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkRenderingAttachmentInfo color{};
    VkRenderingAttachmentInfo depth{};
    VkRenderingInfo info{};
    bool open = false;

    RenderingPass() = default;
    RenderingPass(const RenderingPass&) = delete;
    RenderingPass& operator=(const RenderingPass&) = delete;

    RenderingPass(RenderingPass&& other) noexcept { take(other); }
    RenderingPass& operator=(RenderingPass&& other) noexcept {
        if (this != &other) {
            close();
            take(other);
        }
        return *this;
    }

    ~RenderingPass() { close(); }

    void close() {
        if (open && cmd != VK_NULL_HANDLE) {
            vkCmdEndRendering(cmd);
        }
        open = false;
        cmd = VK_NULL_HANDLE;
    }

    void retarget() {
        info.pColorAttachments = info.colorAttachmentCount > 0 ? &color : nullptr;
        info.pDepthAttachment = depth.imageView != VK_NULL_HANDLE ? &depth : nullptr;
    }

    void take(RenderingPass& other) {
        cmd = other.cmd;
        color = other.color;
        depth = other.depth;
        info = other.info;
        open = other.open;
        other.open = false;
        other.cmd = VK_NULL_HANDLE;
        retarget();
    }
};

using RenderingScope = RenderingPass;

inline void endRendering(RenderingPass& pass) { pass.close(); }

inline void fillAttachment(
    VkRenderingAttachmentInfo& out,
    VkImageView view,
    VkImageLayout layout,
    VkAttachmentLoadOp load,
    VkClearValue clear) {
    out = {};
    out.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    out.imageView = view;
    out.imageLayout = layout;
    out.loadOp = load;
    out.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    out.clearValue = clear;
}

[[nodiscard]] inline RenderingPass beginRenderingColorDepth(
    VkCommandBuffer cmd,
    VkExtent2D extent,
    VkImageView colorView,
    VkImageView depthView,
    VkClearValue clearColor,
    VkClearValue clearDepth,
    bool clearOnLoad = true) {
    RenderingPass pass;
    pass.cmd = cmd;
    const VkAttachmentLoadOp load = clearOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    if (colorView != VK_NULL_HANDLE) {
        fillAttachment(pass.color, colorView, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, load, clearColor);
    }
    if (depthView != VK_NULL_HANDLE) {
        fillAttachment(pass.depth, depthView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, load, clearDepth);
    }
    pass.info = {};
    pass.info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.info.renderArea = {{0, 0}, extent};
    pass.info.layerCount = 1;
    pass.info.colorAttachmentCount = colorView != VK_NULL_HANDLE ? 1u : 0u;
    pass.retarget();
    vkCmdBeginRendering(cmd, &pass.info);
    pass.open = true;
    return pass;
}

// renderArea с нулевой шириной значит весь extent.
[[nodiscard]] inline RenderingPass beginRenderingDepthOnly(
    VkCommandBuffer cmd,
    VkExtent2D extent,
    VkImageView depthView,
    VkClearValue clearDepth,
    bool clearOnLoad = true,
    VkRect2D renderArea = {}) {
    RenderingPass pass;
    pass.cmd = cmd;
    const VkAttachmentLoadOp load = clearOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    fillAttachment(pass.depth, depthView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, load, clearDepth);
    if (renderArea.extent.width == 0) {
        renderArea = {{0, 0}, extent};
    }
    pass.info = {};
    pass.info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    pass.info.renderArea = renderArea;
    pass.info.layerCount = 1;
    pass.info.colorAttachmentCount = 0;
    pass.retarget();
    vkCmdBeginRendering(cmd, &pass.info);
    pass.open = true;
    return pass;
}

} // namespace burnhope
