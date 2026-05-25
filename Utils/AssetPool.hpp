#pragma once

#include "../Render/Texture.hpp"
#include "Core/Types.hpp"
#include <memory>
#include <vector>

namespace burnhope {

class BindlessRegistry;

using TextureHandle = uint32_t;
using MaterialHandle = uint32_t;

inline constexpr TextureHandle kInvalidTextureHandle = UINT32_MAX;
inline constexpr MaterialHandle kInvalidMaterialHandle = UINT32_MAX;

/** Off hot path: stable handles into GPU texture pool + heap slots. */
class TexturePool final : public NonCopyable {
public:
  TextureHandle emplace(std::unique_ptr<BurnhopeTexture> texture, BindlessRegistry& bindless);
  [[nodiscard]] BurnhopeTexture* resolve(TextureHandle handle) const noexcept;
  [[nodiscard]] uint32_t heapIndex(TextureHandle handle) const noexcept;
  [[nodiscard]] uint32_t samplerHeapIndex(TextureHandle handle) const noexcept;
  [[nodiscard]] uint32_t count() const noexcept {
    return static_cast<uint32_t>(entries_.size());
  }

  /** Transfers ownership out (e.g. deferred GPU delete queue). */
  std::unique_ptr<BurnhopeTexture> release(TextureHandle handle);

  /** Re-register all textures after descriptor heap reset (GPU must be idle). */
  void refreshHeapIndices(BindlessRegistry& bindless);

private:
  struct Entry {
    std::unique_ptr<BurnhopeTexture> texture;
    uint32_t imageHeapIndex{kInvalidTextureHandle};
    uint32_t samplerHeapIndex{kInvalidTextureHandle};
  };
  std::vector<Entry> entries_;
};

class Material;

class MaterialPool final : public NonCopyable {
public:
  MaterialHandle emplace(std::unique_ptr<Material> material);
  [[nodiscard]] Material* resolve(MaterialHandle handle) const noexcept;

private:
  std::vector<std::unique_ptr<Material>> materials_;
};

} // namespace burnhope
