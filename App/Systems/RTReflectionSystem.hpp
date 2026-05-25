#pragma once

#include "../../Render/Core/ComputeDispatch.hpp"
#include "../../Render/Texture.hpp"
#include "../../Utils/BindlessRegistry.hpp"
#include "../../Utils/Buffer.hpp"
#include "../../Utils/Device.hpp"
#include <memory>
#include <vector>

namespace burnhope {

class RTReflectionSystem final {
public:
  RTReflectionSystem(BurnhopeDevice& device, BindlessRegistry& bindless);

  void init(VkExtent2D extent);
  void updateDescriptors(VkExtent2D extent, TextureHandle defaultWhite);

  std::unique_ptr<BurnhopeTexture> rtReflectionsTexture;
  std::unique_ptr<ComputeDispatch> rtReflectionsShader;
  std::unique_ptr<ComputeDispatch> probeRenderShader;
  std::vector<std::unique_ptr<BurnhopeTexture>> probeTextures;
  std::unique_ptr<BurnhopeBuffer> probesBuffer;
  uint32_t rtHeapIndex{0};

private:
  BurnhopeDevice& device_;
  BindlessRegistry& bindless_;
};

} // namespace burnhope
