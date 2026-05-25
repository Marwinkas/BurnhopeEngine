#include "AssetPool.hpp"
#include "../Render/Material.hpp"
#include "BindlessRegistry.hpp"

namespace burnhope {

TextureHandle TexturePool::emplace(
    std::unique_ptr<BurnhopeTexture> texture,
    BindlessRegistry& bindless) {
  Entry entry;
  entry.texture = std::move(texture);
  entry.imageHeapIndex = bindless.registerSampledImage(*entry.texture);
  const TextureHandle handle = static_cast<TextureHandle>(entries_.size());
  entries_.push_back(std::move(entry));
  return handle;
}

BurnhopeTexture* TexturePool::resolve(TextureHandle handle) const noexcept {
  if (handle >= entries_.size()) {
    return nullptr;
  }
  return entries_[handle].texture.get();
}

uint32_t TexturePool::heapIndex(TextureHandle handle) const noexcept {
  if (handle >= entries_.size()) {
    return 0;
  }
  return entries_[handle].imageHeapIndex;
}

uint32_t TexturePool::samplerHeapIndex(TextureHandle handle) const noexcept {
  if (handle >= entries_.size()) {
    return 0;
  }
  return entries_[handle].samplerHeapIndex;
}

std::unique_ptr<BurnhopeTexture> TexturePool::release(TextureHandle handle) {
  if (handle >= entries_.size()) {
    return nullptr;
  }
  return std::move(entries_[handle].texture);
}

void TexturePool::refreshHeapIndices(BindlessRegistry& bindless) {
  for (Entry& entry : entries_) {
    if (entry.texture) {
      entry.imageHeapIndex = bindless.registerSampledImage(*entry.texture);
    }
  }
}

MaterialHandle MaterialPool::emplace(std::unique_ptr<Material> material) {
  const MaterialHandle h = static_cast<MaterialHandle>(materials_.size());
  materials_.push_back(std::move(material));
  return h;
}

Material* MaterialPool::resolve(MaterialHandle handle) const noexcept {
  if (handle >= materials_.size()) {
    return nullptr;
  }
  return materials_[handle].get();
}

} // namespace burnhope
