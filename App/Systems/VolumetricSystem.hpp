#pragma once

#include "../../Render/Core/ComputeDispatch.hpp"
#include "../../Render/Texture.hpp"
#include "../../Utils/BindlessRegistry.hpp"
#include "../../Utils/Device.hpp"
#include <memory>

namespace burnhope {

class VolumetricSystem final {
public:
  VolumetricSystem(BurnhopeDevice& device, BindlessRegistry& bindless);

  void init(VkExtent2D extent);
  void updateDescriptors(VkExtent2D extent);

  std::unique_ptr<BurnhopeTexture> volumetricTex;
  std::unique_ptr<ComputeDispatch> shader;
  uint32_t volumetricHeapIndex{0};
  uint32_t volumetricSampledHeapIndex{0};

private:
  BurnhopeDevice& device_;
  BindlessRegistry& bindless_;
};

} // namespace burnhope
