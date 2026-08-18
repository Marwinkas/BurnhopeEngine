#pragma once
#include "../../Render/Texture.hpp"
#include "../../Render/Material.hpp"
#include "../../Render/ComputeShader.hpp"
#include "../Descriptors.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace burnhope::ui {

    // Editor-only: raymarched PBR sphere via shaders/mat_preview.comp.
    // Dispatches with single-time commands (never inside the swapchain pass).
    class MaterialPreview {
    public:
        explicit MaterialPreview(BurnhopeDevice& device);
        ~MaterialPreview();

        MaterialPreview(const MaterialPreview&) = delete;
        MaterialPreview& operator=(const MaterialPreview&) = delete;

        // Live editor target (256²). Re-renders when `mat` contents change.
        void UpdateEditor(Material& mat);
        VkImageView EditorView() const { return m_Editor ? m_Editor->getImageView() : VK_NULL_HANDLE; }
        VkSampler EditorSampler() const { return m_Editor ? m_Editor->getSampler() : VK_NULL_HANDLE; }

        // Cached 128² thumb for Content Browser. Null if the file cannot load.
        BurnhopeTexture* Thumb(const std::string& matPath);
        void Invalidate(const std::string& matPath) { m_Thumbs.erase(matPath); }
        VkImageLayout PreviewLayout() const { return VK_IMAGE_LAYOUT_GENERAL; }

    private:
        struct Push {
            glm::vec4 albedoColor;
            glm::vec4 emissiveColor;
            glm::vec4 matParams;
            glm::vec4 uvScaleTri;
            glm::vec4 camPosTime;
            glm::ivec4 flags;
        };

        std::unique_ptr<BurnhopeTexture> MakeSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t w = 1, uint32_t h = 1);
        std::unique_ptr<BurnhopeTexture> MakeTarget(uint32_t size);
        std::unique_ptr<BurnhopeTexture> MakeHdr();
        void RenderInto(BurnhopeTexture& target, Material& mat, uint32_t size);
        uint64_t HashMat(const Material& mat) const;

        BurnhopeDevice& m_Device;
        std::unique_ptr<BurnhopeTexture> m_White;
        std::unique_ptr<BurnhopeTexture> m_FlatNormal;
        std::unique_ptr<BurnhopeTexture> m_DefaultOrm;
        std::unique_ptr<BurnhopeTexture> m_Hdr;
        std::unique_ptr<BurnhopeTexture> m_Editor;
        uint64_t m_EditorHash = 0;

        std::unique_ptr<BurnhopeDescriptorSetLayout> m_Layout;
        std::unique_ptr<BurnhopeDescriptorPool> m_Pool;
        std::unique_ptr<ComputeShader> m_Shader;
        VkDescriptorSet m_Set = VK_NULL_HANDLE;

        std::unordered_map<std::string, std::unique_ptr<BurnhopeTexture>> m_Thumbs;
    };
}
