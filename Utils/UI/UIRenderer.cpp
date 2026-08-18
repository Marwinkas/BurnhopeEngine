#include "UIRenderer.hpp"
#include "../../Render/Texture.hpp"
#include <stdexcept>
#include <cstring>

namespace burnhope::ui {

UIRenderer::UIRenderer(BurnhopeDevice& device, VkFormat colorFormat, uint32_t maxInstances)
    : m_Device(device), m_MaxInstances(maxInstances) {
    m_Instances.reserve(maxInstances);
    m_OverlayInstances.reserve(4096);
    m_TextureSlots.resize(kMaxUITextures);

    CreateWhiteTexture();
    CreateDescriptorLayout();
    CreatePipeline(colorFormat);

    for (int i = 0; i < kFramesInFlight; ++i) {
        m_InstanceBuffers[i] = std::make_unique<BurnhopeBuffer>(
            device, sizeof(UIInstance), maxInstances,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_InstanceBuffers[i]->map();

        m_DescriptorPool->allocateDescriptor(m_SetLayout->getDescriptorSetLayout(), m_DescriptorSets[i]);

        VkDescriptorBufferInfo bufInfo = m_InstanceBuffers[i]->descriptorInfo();
        BurnhopeDescriptorWriter(*m_SetLayout, *m_DescriptorPool)
            .writeBuffer(0, &bufInfo)
            .writeImageArray(1, m_TextureSlots)
            .overwrite(m_DescriptorSets[i]);
    }
}

UIRenderer::~UIRenderer() {
    if (m_PipelineLayout) vkDestroyPipelineLayout(m_Device.device(), m_PipelineLayout, nullptr);
}

void UIRenderer::CreateWhiteTexture() {
    m_Sampler = VK_NULL_HANDLE; // set below via the texture's own sampler

    m_WhiteTexture = std::make_unique<BurnhopeTexture>(m_Device, VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{1, 1, 1},
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                         VK_SAMPLE_COUNT_1_BIT);
    BurnhopeTexture* whiteTex = m_WhiteTexture.get();

    BurnhopeBuffer staging(m_Device, 4, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.map();
    uint8_t white[4] = {255, 255, 255, 255};
    staging.writeToBuffer(white, 4);
    staging.unmap();

    // BurnhopeTexture::transitionLayout owns and submits the one-shot
    // command buffer. Keep the copy in its own recording scope instead of
    // appending commands to the already-submitted handle.
    whiteTex->transitionLayout(m_Device.beginSingleTimeCommands(),
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkCommandBuffer cmd = m_Device.beginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(cmd, staging.getBuffer(), whiteTex->getImage(),
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    m_Device.endSingleTimeCommands(cmd);
    whiteTex->transitionLayout(m_Device.beginSingleTimeCommands(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    m_WhiteImage = whiteTex->getImage();
    m_WhiteView = whiteTex->getImageView();
    m_Sampler = whiteTex->getSampler();

    // Fill every texture slot with the white pixel by default so the
    // descriptor array is always fully valid (unused slots simply sample
    // white). RegisterTexture overwrites individual slots afterwards.
    VkDescriptorImageInfo defaultInfo{};
    defaultInfo.sampler = m_Sampler;
    defaultInfo.imageView = m_WhiteView;
    defaultInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for (auto& slot : m_TextureSlots) slot = defaultInfo;
}

void UIRenderer::CreateDescriptorLayout() {
    m_SetLayout = BurnhopeDescriptorSetLayout::Builder(m_Device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, kMaxUITextures)
        .build();

    m_DescriptorPool = BurnhopeDescriptorPool::Builder(m_Device)
        .setMaxSets(kFramesInFlight)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kFramesInFlight)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight * kMaxUITextures)
        .build();
}

void UIRenderer::CreatePipeline(VkFormat colorFormat) {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    VkDescriptorSetLayout setLayout = m_SetLayout->getDescriptorSetLayout();
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::vec2);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(m_Device.device(), &layoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("UIRenderer: failed to create pipeline layout");
    }

    PipelineConfigInfo config{};
    BurnhopePipeline::defaultPipelineConfigInfo(config);
    BurnhopePipeline::enableAlphaBlending(config);
    config.bindingDescriptions.clear();
    config.attributeDescriptions.clear();
    config.depthStencilInfo.depthTestEnable = VK_FALSE;
    config.depthStencilInfo.depthWriteEnable = VK_FALSE;
    config.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
    config.colorAttachmentFormats = {colorFormat};
    config.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
    config.pipelineLayout = m_PipelineLayout;

    m_Pipeline = std::make_unique<BurnhopePipeline>(
        m_Device, std::vector<std::string>{"shaders/ui_quad.vert.spv", "shaders/ui_quad.frag.spv"}, config);
}

uint32_t UIRenderer::RegisterTexture(VkImageView view, VkSampler sampler, VkImageLayout layout) {
    if (!view) return 0;
    const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(view)) *
                          0x9E3779B97F4A7C15ULL) ^
                         static_cast<uint64_t>(reinterpret_cast<uintptr_t>(sampler));
    if (auto it = m_TextureIndices.find(key); it != m_TextureIndices.end()) return it->second;
    if (m_NextTextureIndex >= kMaxUITextures) return 0; // fall back to white
    uint32_t index = m_NextTextureIndex++;
    m_TextureIndices.emplace(key, index);

    VkDescriptorImageInfo info{};
    info.sampler = sampler ? sampler : m_Sampler;
    info.imageView = view;
    info.imageLayout = layout;
    m_TextureSlots[index] = info;

    for (int i = 0; i < kFramesInFlight; ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_DescriptorSets[i];
        write.dstBinding = 1;
        write.dstArrayElement = index;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &info;
        vkUpdateDescriptorSets(m_Device.device(), 1, &write, 0, nullptr);
    }
    return index;
}

void UIRenderer::BeginFrame(glm::vec2 screenSizePixels) {
    m_ScreenSize = screenSizePixels;
    m_Instances.clear();
    m_OverlayInstances.clear();
    m_WritingOverlay = false;
}

void UIRenderer::PushInstance(const UIInstance& instance) {
    auto& dst = m_WritingOverlay ? m_OverlayInstances : m_Instances;
    if (dst.size() >= m_MaxInstances) return;
    dst.push_back(instance);
}

void UIRenderer::Render(VkCommandBuffer cmd) {
    if (!m_OverlayInstances.empty()) {
        m_Instances.insert(m_Instances.end(), m_OverlayInstances.begin(), m_OverlayInstances.end());
        m_OverlayInstances.clear();
    }
    if (m_Instances.empty()) return;

    m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
    auto& buffer = m_InstanceBuffers[m_FrameIndex];
    buffer->writeToBuffer(m_Instances.data(), m_Instances.size() * sizeof(UIInstance));
    buffer->flush(m_Instances.size() * sizeof(UIInstance));

    m_Pipeline->bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout, 0, 1,
                             &m_DescriptorSets[m_FrameIndex], 0, nullptr);
    vkCmdPushConstants(cmd, m_PipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2), &m_ScreenSize);
    vkCmdDraw(cmd, 6, static_cast<uint32_t>(m_Instances.size()), 0, 0);
}
}
