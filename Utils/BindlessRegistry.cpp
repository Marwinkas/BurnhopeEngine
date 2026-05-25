#include "BindlessRegistry.hpp"
#include "BindlessPush.hpp"
#include "../Render/Texture.hpp"
#include "VkExtDispatch.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace burnhope {

namespace {

VkSamplerCreateInfo makeLinearClampSamplerInfo() {
  VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.maxAnisotropy = 1.0f;
  info.anisotropyEnable = VK_FALSE;
  info.compareEnable = VK_FALSE;
  info.minLod = 0.0f;
  info.maxLod = VK_LOD_CLAMP_NONE;
  return info;
}

VkImageViewCreateInfo viewInfoFromTexture(const BurnhopeTexture& tex) {
  VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view.image = tex.getImage();
  view.viewType = tex.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
  view.format = tex.getFormat();
  const bool isDepth = view.format == VK_FORMAT_D32_SFLOAT || view.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                     view.format == VK_FORMAT_D24_UNORM_S8_UINT;
  // Sampled depth: depth aspect only (Vulkan forbids DEPTH|STENCIL for sampled images).
  view.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
  view.subresourceRange.baseMipLevel = 0;
  view.subresourceRange.levelCount = 1;
  view.subresourceRange.baseArrayLayer = 0;
  view.subresourceRange.layerCount = tex.arrayLayers;
  return view;
}

} // namespace

BindlessRegistry::BindlessRegistry(BurnhopeDevice& device)
    : device_{device}, heap_{device}, defaultSamplerInfo_{makeLinearClampSamplerInfo()} {
  slots_.defaultSampler = registerSampler(defaultSamplerInfo_);
}

uint32_t BindlessRegistry::byteOffsetToSlot(VkDeviceSize byteOffset) const noexcept {
  const VkDeviceSize stride = heap_.resourceStride();
  if (stride == 0) {
    return 0;
  }
  return static_cast<uint32_t>(byteOffset / stride);
}

uint32_t BindlessRegistry::registerUniformBuffer(
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range) {
  const VkDeviceSize byteOff = heap_.writeBuffer(
      buffer, offset, range, VK_FORMAT_UNDEFINED, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  return byteOffsetToSlot(byteOff);
}

uint32_t BindlessRegistry::registerStorageBuffer(
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range) {
  const VkDeviceSize byteOff = heap_.writeBuffer(buffer, offset, range, VK_FORMAT_UNDEFINED);
  return byteOffsetToSlot(byteOff);
}

uint32_t BindlessRegistry::registerStorageImage(BurnhopeTexture& texture, VkImageLayout layout) {
  const VkImageViewCreateInfo viewInfo = viewInfoFromTexture(texture);
  const VkDeviceSize byteOff =
      heap_.writeImage(viewInfo, layout, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
  return byteOffsetToSlot(byteOff);
}

uint32_t BindlessRegistry::registerSampledImage(BurnhopeTexture& texture, VkImageLayout layout) {
  const VkImageViewCreateInfo viewInfo = viewInfoFromTexture(texture);
  const VkDeviceSize byteOff =
      heap_.writeImage(viewInfo, layout, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
  return byteOffsetToSlot(byteOff);
}

uint32_t BindlessRegistry::registerSampler(VkSamplerCreateInfo createInfo) {
  const VkDeviceSize byteOff = heap_.writeSampler(createInfo);
  const VkDeviceSize stride = device_.descriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
  return static_cast<uint32_t>(byteOff / stride);
}

uint32_t BindlessRegistry::registerAccelerationStructure(VkAccelerationStructureKHR as) {
  if (as == VK_NULL_HANDLE) {
    return 0;
  }
  const VkDeviceSize byteOff = heap_.writeAccelerationStructure(as);
  return byteOffsetToSlot(byteOff);
}

void BindlessRegistry::bind(VkCommandBuffer cmd) const { heap_.bind(cmd); }

void BindlessRegistry::pushHeapDataAt(
    VkCommandBuffer cmd,
    uint32_t byteOffset,
    const void* data,
    uint32_t size) const {
  if (!data || size == 0) {
    return;
  }
  auto pfn = vkext::get().cmdPushDataEXT;
  if (!pfn) {
    pfn = reinterpret_cast<PFN_vkCmdPushDataEXT>(
        vkGetDeviceProcAddr(device_.device(), "vkCmdPushDataEXT"));
  }
  if (!pfn) {
    throwVkError("BindlessRegistry: vkCmdPushDataEXT unavailable");
  }
  const uint32_t maxPush = device_.caps().maxPushDataSize;
  if (byteOffset >= maxPush) {
    return;
  }
  alignas(16) std::array<uint8_t, 288> padded{};
  const uint32_t room = maxPush - byteOffset;
  const uint32_t copyBytes = std::min(size, std::min(room, static_cast<uint32_t>(padded.size())));
  if (copyBytes == 0) {
    return;
  }
  std::memcpy(padded.data(), data, copyBytes);
  uint32_t pushBytes = (copyBytes + 3u) & ~3u;
  pushBytes = std::min(pushBytes, room);
  pushBytes = std::min(pushBytes, static_cast<uint32_t>(padded.size()));
  VkPushDataInfoEXT info{VK_STRUCTURE_TYPE_PUSH_DATA_INFO_EXT};
  info.offset = byteOffset;
  info.data = {padded.data(), pushBytes};
  pfn(cmd, &info);
}

void BindlessRegistry::pushHeapData(VkCommandBuffer cmd, const void* data, uint32_t size) const {
  pushHeapDataAt(cmd, 0, data, size);
}

void BindlessRegistry::pushAndBind(VkCommandBuffer cmd, const void* data, uint32_t size) const {
  pushHeapData(cmd, data, size);
  const uint32_t heapBase = 0u;
  pushHeapDataAt(cmd, kGfxWireHeapResourceBase, &heapBase, 4);
  bind(cmd);
}

bool BindlessRegistry::verifyStorageBufferSlot(
    uint32_t slot,
    VkBuffer buffer,
    VkDeviceSize bufferOffset,
    const char* label) const noexcept {
  if (buffer == VK_NULL_HANDLE || slot == 0) {
    return false;
  }
  const VkDeviceSize stride = heap_.resourceStride();
  const VkDeviceSize heapByteOffset = static_cast<VkDeviceSize>(slot) * stride;
  const VkDeviceAddress expected = device_.bufferDeviceAddress(buffer) + bufferOffset;
  const auto result = heap_.verifyStorageBufferDescriptor(heapByteOffset, expected);
  std::cerr << "[HeapVerify] " << (label ? label : "ssbo")
            << " slot=" << slot
            << " heapOff=" << heapByteOffset
            << " expected=0x" << std::hex << expected
            << " found=0x" << result.found
            << " match=" << std::dec << (result.addressMatch ? "OK" : "FAIL");
  if (result.addressMatch) {
    std::cerr << " @byte+" << result.matchByteOffset;
  } else if (const void* host = heap_.resourceHostBytes()) {
    const auto* bytes = static_cast<const std::byte*>(host) + heapByteOffset;
    std::cerr << " raw[0..15]=";
    for (uint32_t i = 0; i < 16 && i < stride; ++i) {
      std::cerr << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<unsigned>(static_cast<uint8_t>(bytes[i])) << ' ';
    }
    std::cerr << std::dec;
  }
  std::cerr << '\n';
  return result.addressMatch;
}

} // namespace burnhope
