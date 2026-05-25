#pragma once

#include "../Render/ModelImporter.h"
#include "../Utils/DirectXMathCompat.hpp"
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace burnhope {

struct SkeletonData {
  BHBoneHeader header{};
  std::vector<BHBoneNode> bones;
};

struct AnimationData {
  BHAnimHeader header{};
  std::vector<BHAnimTrack> tracks;
  std::vector<BHKeyframeCompressed> keyframes;
};

/** Off hot path: binary skeleton/animation load + evaluation. */
class AnimationSystem {
public:
  std::shared_ptr<SkeletonData> loadSkeleton(const std::string& path);
  std::shared_ptr<AnimationData> loadAnimation(const std::string& path);

  static quat unpackQuat(uint32_t packed);
  void evaluate(
      const SkeletonData& skel,
      const AnimationData& anim,
      float time,
      std::vector<float4x4>& outMatrices);

private:
  std::unordered_map<std::string, std::shared_ptr<SkeletonData>> skeletons_;
  std::unordered_map<std::string, std::shared_ptr<AnimationData>> animations_;
};

} // namespace burnhope
