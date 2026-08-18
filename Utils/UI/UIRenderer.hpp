#pragma once
#include "UICore.hpp"
#include "../Device.hpp"
#include "../Buffer.hpp"
#include "../Descriptors.hpp"
#include "../Pipeline.hpp"
#include "../../Render/Texture.hpp"
#include <memory>
#include <vector>
#include <array>
#include <unordered_map>

// GPU-driven quad renderer for the editor UI: one instanced draw per frame,
// bindless texture array (solid color / image / SDF glyph atlas), instance
// data written straight into a host-visible SSBO (ReBAR-style) each frame —
// replaces ImGui_ImplVulkan's per-vertex draw-list submission.
namespace burnhope::ui {

    inline constexpr uint32_t kMaxUITextures = 256;

    class UIRenderer {
    public:
        UIRenderer(BurnhopeDevice& device, VkFormat colorFormat, uint32_t maxInstances = 65536);
        ~UIRenderer();

        UIRenderer(const UIRenderer&) = delete;
        UIRenderer& operator=(const UIRenderer&) = delete;

        // Registers a sampled image in the bindless texture array; returns
        // the index to place into UIInstance::textureIndex. Index 0 is
        // reserved for a 1x1 white pixel (solid color quads).
        uint32_t RegisterTexture(VkImageView view, VkSampler sampler,
                                 VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        void BeginFrame(glm::vec2 screenSizePixels);
        // Overlay instances are drawn after regular UI so menus/popups sit
        // on top of later dock panels.
        void SetOverlay(bool overlay) { m_WritingOverlay = overlay; }
        void PushInstance(const UIInstance& instance);
        // Records the instanced draw call. Must be called while a dynamic
        // rendering pass targeting `colorFormat` is already active (the
        // swapchain pass set up by BurnhopeRenderer::beginSwapChainRendering).
        void Render(VkCommandBuffer cmd);

        uint32_t InstanceCount() const { return static_cast<uint32_t>(m_Instances.size()); }

    private:
        void CreateWhiteTexture();
        void CreateDescriptorLayout();
        void CreatePipeline(VkFormat colorFormat);

        BurnhopeDevice& m_Device;
        uint32_t m_MaxInstances;
        glm::vec2 m_ScreenSize{1.0f, 1.0f};

        std::vector<UIInstance> m_Instances;
        std::vector<UIInstance> m_OverlayInstances;
        bool m_WritingOverlay = false;

        static constexpr int kFramesInFlight = 3;
        std::array<std::unique_ptr<BurnhopeBuffer>, kFramesInFlight> m_InstanceBuffers;
        int m_FrameIndex = 0;

        std::unique_ptr<BurnhopeDescriptorSetLayout> m_SetLayout;
        std::unique_ptr<BurnhopeDescriptorPool> m_DescriptorPool;
        std::array<VkDescriptorSet, kFramesInFlight> m_DescriptorSets{};

        VkSampler m_Sampler = VK_NULL_HANDLE;
        VkImageView m_WhiteView = VK_NULL_HANDLE;
        VkImage m_WhiteImage = VK_NULL_HANDLE;
        std::unique_ptr<burnhope::BurnhopeTexture> m_WhiteTexture;
        std::vector<VkDescriptorImageInfo> m_TextureSlots;
        std::unordered_map<uint64_t, uint32_t> m_TextureIndices;
        uint32_t m_NextTextureIndex = 1; // 0 == white pixel

        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<BurnhopePipeline> m_Pipeline;
    };
}
