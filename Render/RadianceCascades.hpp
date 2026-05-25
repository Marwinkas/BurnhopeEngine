#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "Texture.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include "Core/ComputeDispatch.hpp"
#include <memory>

namespace burnhope {

struct RCConfig {
  static constexpr int CASCADE_COUNT = 4;
  static constexpr int PROBE_X = 32;
  static constexpr int PROBE_Y = 16;
  static constexpr int PROBE_Z = 32;
  static constexpr int OCTA_SIZE = 16;
  static constexpr float BASE_RAY_LENGTH = 1.5f;
};

class RadianceCascadesSystem final {
public:
  RadianceCascadesSystem(BurnhopeDevice& device, BindlessRegistry& bindless, VkExtent2D extent);
  ~RadianceCascadesSystem();

  [[nodiscard]] bool needsRebuild(int px, int py, int pz, int octa, float baseLen) const;
  void updateConfig(int px, int py, int pz, int octa, float baseLen);
  void rebuildOnResize(VkExtent2D extent);

  void dispatch(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const CascadeMergePC& mergePush,
      const GiSampleHeapPC& samplePush,
      VkExtent2D screenExtent);

  [[nodiscard]] uint32_t giDiffuseHeap() const noexcept { return giDiffuseHeap_; }
  [[nodiscard]] uint32_t refreshGiHeapSlot(BindlessRegistry& bindless);

private:
  BurnhopeDevice& device_;
  BindlessRegistry& bindless_;
  VkExtent2D extent_{};
  int probeX_{RCConfig::PROBE_X};
  int probeY_{RCConfig::PROBE_Y};
  int probeZ_{RCConfig::PROBE_Z};
  int octaSize_{RCConfig::OCTA_SIZE};
  float baseRayLength_{RCConfig::BASE_RAY_LENGTH};
  uint32_t giDiffuseHeap_{0};
  std::unique_ptr<BurnhopeTexture> giDiffuseTex_;
  std::unique_ptr<ComputeDispatch> mergeShader_;
  std::unique_ptr<ComputeDispatch> sampleShader_;
};

} // namespace burnhope
