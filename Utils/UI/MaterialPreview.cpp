#include "MaterialPreview.hpp"
#include "../Buffer.hpp"
#include <cstring>
#include <cmath>
#include <vector>

namespace burnhope::ui {

namespace {
    void Barrier(VkCommandBuffer cmd, VkImage image,
                 VkImageLayout oldL, VkImageLayout newL,
                 VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                 VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcStageMask = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    VkDescriptorImageInfo Sampled(BurnhopeTexture* tex, BurnhopeTexture* fallback) {
        BurnhopeTexture* t = tex ? tex : fallback;
        VkDescriptorImageInfo i{};
        i.sampler = t->getSampler();
        i.imageView = t->getImageView();
        i.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return i;
    }
}

MaterialPreview::MaterialPreview(BurnhopeDevice& device) : m_Device(device) {
    m_White = MakeSolid(255, 255, 255, 255);
    m_FlatNormal = MakeSolid(128, 128, 255, 255);
    m_DefaultOrm = MakeSolid(255, 255, 0, 255);
    m_Hdr = MakeHdr();
    m_Editor = MakeTarget(256);

    m_Layout = BurnhopeDescriptorSetLayout::Builder(device)
        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    m_Pool = BurnhopeDescriptorPool::Builder(device)
        .setMaxSets(4)
        .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4)
        .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16)
        .build();

    m_Shader = std::make_unique<ComputeShader>(
        device, "shaders/mat_preview.comp.spv",
        std::vector<VkDescriptorSetLayout>{m_Layout->getDescriptorSetLayout()},
        static_cast<uint32_t>(sizeof(Push)));

    m_Pool->allocateDescriptor(m_Layout->getDescriptorSetLayout(), m_Set);
}

MaterialPreview::~MaterialPreview() = default;

std::unique_ptr<BurnhopeTexture> MaterialPreview::MakeSolid(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint32_t w, uint32_t h) {
    auto tex = std::make_unique<BurnhopeTexture>(
        m_Device, VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{w, h, 1},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT);

    const uint32_t n = w * h;
    std::vector<uint8_t> pixels(n * 4);
    for (uint32_t i = 0; i < n; ++i) {
        pixels[i * 4 + 0] = r;
        pixels[i * 4 + 1] = g;
        pixels[i * 4 + 2] = b;
        pixels[i * 4 + 3] = a;
    }
    BurnhopeBuffer staging(m_Device, pixels.size(), 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.map();
    staging.writeToBuffer(pixels.data(), pixels.size());
    staging.unmap();

    tex->transitionLayout(m_Device.beginSingleTimeCommands(),
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_Device.copyBufferToImage(staging.getBuffer(), tex->getImage(), w, h, 1);
    tex->transitionLayout(m_Device.beginSingleTimeCommands(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return tex;
}

std::unique_ptr<BurnhopeTexture> MaterialPreview::MakeHdr() {
    constexpr uint32_t k = 32;
    std::vector<uint8_t> pixels(k * k * 4);
    for (uint32_t y = 0; y < k; ++y) {
        float t = static_cast<float>(y) / static_cast<float>(k - 1);
        uint8_t r = static_cast<uint8_t>(40 + (220 - 40) * (1.0f - t));
        uint8_t g = static_cast<uint8_t>(50 + (180 - 50) * (1.0f - t));
        uint8_t b = static_cast<uint8_t>(70 + (140 - 70) * (1.0f - t) + 40.0f * t);
        for (uint32_t x = 0; x < k; ++x) {
            uint32_t i = (y * k + x) * 4;
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 255;
        }
    }
    auto tex = std::make_unique<BurnhopeTexture>(
        m_Device, VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{k, k, 1},
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT);
    BurnhopeBuffer staging(m_Device, pixels.size(), 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.map();
    staging.writeToBuffer(pixels.data(), pixels.size());
    staging.unmap();
    tex->transitionLayout(m_Device.beginSingleTimeCommands(),
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    m_Device.copyBufferToImage(staging.getBuffer(), tex->getImage(), k, k, 1);
    tex->transitionLayout(m_Device.beginSingleTimeCommands(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return tex;
}

std::unique_ptr<BurnhopeTexture> MaterialPreview::MakeTarget(uint32_t size) {
    auto tex = std::make_unique<BurnhopeTexture>(
        m_Device, VK_FORMAT_R8G8B8A8_UNORM, VkExtent3D{size, size, 1},
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SAMPLE_COUNT_1_BIT);
    tex->transitionLayout(m_Device.beginSingleTimeCommands(),
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    return tex;
}

uint64_t MaterialPreview::HashMat(const Material& mat) const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    auto mixf = [&](float f) { uint32_t u; std::memcpy(&u, &f, 4); mix(u); };
    auto mixs = [&](const std::string& s) { for (unsigned char c : s) mix(c); };
    mixs(mat.albedoPath); mixs(mat.normalPath); mixs(mat.ormPath);
    mixs(mat.roughnessPath); mixs(mat.metallicPath); mixs(mat.aoPath);
    mixs(mat.emissivePath); mixs(mat.alphaPath);
    mixf(mat.albedoColor.x); mixf(mat.albedoColor.y); mixf(mat.albedoColor.z); mixf(mat.albedoColor.w);
    mixf(mat.metallicStrength); mixf(mat.roughnessStrength); mixf(mat.normalStrength);
    mixf(mat.emissiveIntensity); mixf(mat.uvScale.x); mixf(mat.uvScale.y);
    mix(mat.useTriplanar ? 1 : 0); mix(mat.repeatTexture ? 1 : 0); mix(mat.isTransparent ? 1 : 0);
    mix(mat.packedAlbedoAlpha ? 1 : 0); mix(mat.packedORMX ? 1 : 0); mix(mat.packedNormal ? 1 : 0);
    return h;
}

void MaterialPreview::RenderInto(BurnhopeTexture& target, Material& mat, uint32_t size) {
    BurnhopeTexture* albedo = mat.packedAlbedoAlpha ? mat.packedAlbedoAlpha.get()
                           : (mat.hasAlbedo ? mat.albedoMap.get() : nullptr);
    BurnhopeTexture* orm = mat.packedORMX ? mat.packedORMX.get()
                         : (mat.hasORM ? mat.ormMap.get() : nullptr);
    BurnhopeTexture* nrm = mat.packedNormal ? mat.packedNormal.get()
                         : (mat.hasNormal ? mat.normalMap.get() : nullptr);

    VkDescriptorImageInfo storage{};
    storage.imageView = target.getImageView();
    storage.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storage.sampler = VK_NULL_HANDLE;

    VkDescriptorImageInfo alb = Sampled(albedo, m_White.get());
    VkDescriptorImageInfo ormI = Sampled(orm, m_DefaultOrm.get());
    VkDescriptorImageInfo nrmI = Sampled(nrm, m_FlatNormal.get());
    VkDescriptorImageInfo hdr = Sampled(m_Hdr.get(), m_White.get());

    BurnhopeDescriptorWriter(*m_Layout, *m_Pool)
        .writeImage(0, &storage)
        .writeImage(1, &alb)
        .writeImage(2, &ormI)
        .writeImage(3, &nrmI)
        .writeImage(4, &hdr)
        .overwrite(m_Set);

    Push pc{};
    pc.albedoColor = mat.albedoColor;
    pc.emissiveColor = glm::vec4(mat.emissiveColor, mat.emissiveIntensity);
    pc.matParams = glm::vec4(mat.metallicStrength, mat.roughnessStrength, mat.normalStrength, mat.heightStrength);
    pc.uvScaleTri = glm::vec4(mat.uvScale.x, mat.uvScale.y, mat.triplanarScale, 0.0f);
    pc.camPosTime = glm::vec4(0.0f, 0.35f, 3.4f, 0.9f);
    int bits = 0;
    if (albedo) bits |= 1;
    if (orm) bits |= 2;
    if (nrm) bits |= 4;
    if (mat.isTransparent) bits |= 8;
    if (mat.useTriplanar) bits |= 16;
    if (mat.repeatTexture) bits |= 32;
    pc.flags = glm::ivec4(0, bits, 0, 0);

    VkCommandBuffer cmd = m_Device.beginSingleTimeCommands();
    m_Shader->bind(cmd);
    m_Shader->bindDescriptorSets(cmd, {m_Set});
    m_Shader->pushConstants(cmd, &pc, sizeof(pc));
    m_Shader->dispatch(cmd, (size + 15) / 16, (size + 15) / 16, 1);
    Barrier(cmd, target.getImage(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    m_Device.endSingleTimeCommands(cmd);
}

void MaterialPreview::UpdateEditor(Material& mat) {
    uint64_t h = HashMat(mat);
    if (h == m_EditorHash && m_Editor) return;
    m_EditorHash = h;
    RenderInto(*m_Editor, mat, 256);
}

BurnhopeTexture* MaterialPreview::Thumb(const std::string& matPath) {
    if (auto it = m_Thumbs.find(matPath); it != m_Thumbs.end()) return it->second.get();
    try {
        auto mat = Material::loadFromJson(m_Device, matPath);
        if (!mat) return nullptr;
        auto target = MakeTarget(128);
        RenderInto(*target, *mat, 128);
        BurnhopeTexture* ptr = target.get();
        m_Thumbs[matPath] = std::move(target);
        return ptr;
    } catch (...) {
        return nullptr;
    }
}

}
