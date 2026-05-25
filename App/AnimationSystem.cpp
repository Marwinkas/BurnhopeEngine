#include "AnimationSystem.hpp"
#include <cmath>

namespace burnhope {

std::shared_ptr<SkeletonData> AnimationSystem::loadSkeleton(const std::string& path) {
  if (skeletons_.count(path)) {
    return skeletons_[path];
  }
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return nullptr;
  }
  auto skel = std::make_shared<SkeletonData>();
  file.read(reinterpret_cast<char*>(&skel->header), sizeof(BHBoneHeader));
  skel->bones.resize(skel->header.boneCount);
  file.read(
      reinterpret_cast<char*>(skel->bones.data()),
      skel->header.boneCount * sizeof(BHBoneNode));
  skeletons_[path] = skel;
  return skel;
}

std::shared_ptr<AnimationData> AnimationSystem::loadAnimation(const std::string& path) {
  if (animations_.count(path)) {
    return animations_[path];
  }
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return nullptr;
  }
  auto anim = std::make_shared<AnimationData>();
  file.read(reinterpret_cast<char*>(&anim->header), sizeof(BHAnimHeader));
  anim->tracks.resize(anim->header.trackCount);
  file.read(
      reinterpret_cast<char*>(anim->tracks.data()),
      anim->header.trackCount * sizeof(BHAnimTrack));
  anim->keyframes.resize(anim->header.totalKeyframeCount);
  file.read(
      reinterpret_cast<char*>(anim->keyframes.data()),
      anim->header.totalKeyframeCount * sizeof(BHKeyframeCompressed));
  animations_[path] = anim;
  return anim;
}

quat AnimationSystem::unpackQuat(uint32_t packed) {
  const int maxIndex = static_cast<int>(packed & 3);
  constexpr float scale = 0.70710678118f;
  float c[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  float sumSq = 0.0f;
  int shift = 2;
  for (int i = 0; i < 4; i++) {
    if (i == maxIndex) {
      continue;
    }
    const uint32_t quantized = (packed >> shift) & 1023;
    const float normalized = static_cast<float>(quantized) / 1023.0f;
    c[i] = (normalized - 0.5f) * 2.0f * scale;
    sumSq += c[i] * c[i];
    shift += 10;
  }
  c[maxIndex] = std::sqrt(std::max(0.0f, 1.0f - sumSq));
  return quat{c[0], c[1], c[2], c[3]};
}

void AnimationSystem::evaluate(
    const SkeletonData& skel,
    const AnimationData& anim,
    float time,
    std::vector<float4x4>& outMatrices) {
  outMatrices.resize(skel.bones.size(), MatrixIdentity());
  const float duration = anim.header.duration;
  float tps = anim.header.ticksPerSecond;
  if (tps <= 0.0f) {
    tps = 25.0f;
  }
  const float animTime = std::fmod(time * tps, duration);

  std::vector<float4x4> localTransforms(skel.bones.size());
  for (std::size_t i = 0; i < skel.bones.size(); i++) {
    const float4x4 globalBind = MatrixInverse(skel.bones[i].inverseBindMatrix);
    if (skel.bones[i].parentIndex == -1) {
      localTransforms[i] = globalBind;
    } else {
      const float4x4 parentGlobalBind =
          MatrixInverse(skel.bones[skel.bones[i].parentIndex].inverseBindMatrix);
      localTransforms[i] = MatrixMultiply(globalBind, MatrixInverse(parentGlobalBind));
    }
  }

  for (const auto& track : anim.tracks) {
    int boneIdx = -1;
    for (std::size_t i = 0; i < skel.bones.size(); i++) {
      if (skel.bones[i].nameHash == track.boneNameHash) {
        boneIdx = static_cast<int>(i);
        break;
      }
    }
    if (boneIdx == -1) {
      continue;
    }

    const uint32_t first = track.firstKeyframe;
    const uint32_t count = track.keyframeCount;
    if (count == 0) {
      continue;
    }

    uint32_t k0 = 0;
    uint32_t k1 = 0;
    for (uint32_t k = 0; k < count - 1; k++) {
      if (anim.keyframes[first + k + 1].time > animTime) {
        k0 = k;
        k1 = k + 1;
        break;
      }
    }
    if (k0 == 0 && k1 == 0) {
      k0 = count - 1;
      k1 = count - 1;
    }

    const auto& kf0 = anim.keyframes[first + k0];
    const auto& kf1 = anim.keyframes[first + k1];

    float factor = 0.0f;
    if (kf1.time > kf0.time) {
      factor = (animTime - kf0.time) / (kf1.time - kf0.time);
    }

    const float3 p0 = track.posMin +
                    float3{
                        static_cast<float>(kf0.posX),
                        static_cast<float>(kf0.posY),
                        static_cast<float>(kf0.posZ)} /
                        65535.0f * (track.posMax - track.posMin);
    const float3 p1 = track.posMin +
                    float3{
                        static_cast<float>(kf1.posX),
                        static_cast<float>(kf1.posY),
                        static_cast<float>(kf1.posZ)} /
                        65535.0f * (track.posMax - track.posMin);
    const float3 pos = Lerp(p0, p1, factor);

    const quat q0 = unpackQuat(kf0.packedRotation);
    const quat q1 = unpackQuat(kf1.packedRotation);
    const quat rot = QuaternionSlerp(q0, q1, factor);

    localTransforms[boneIdx] = MatrixMultiply(MatrixTranslation(pos), QuaternionToMatrix(rot));
  }

  std::vector<float4x4> globalTransforms(skel.bones.size());
  for (auto& m : globalTransforms) {
    m = MatrixIdentity();
  }
  std::vector<bool> computed(skel.bones.size(), false);

  std::function<float4x4(int)> computeGlobal = [&](int boneIdx) -> float4x4 {
    if (computed[boneIdx]) {
      return globalTransforms[boneIdx];
    }
    float4x4 parentGlobal = MatrixIdentity();
    if (skel.bones[boneIdx].parentIndex != -1) {
      parentGlobal = computeGlobal(skel.bones[boneIdx].parentIndex);
    }
    globalTransforms[boneIdx] = MatrixMultiply(parentGlobal, localTransforms[boneIdx]);
    computed[boneIdx] = true;
    return globalTransforms[boneIdx];
  };

  for (std::size_t i = 0; i < skel.bones.size(); i++) {
    outMatrices[i] = MatrixMultiply(computeGlobal(static_cast<int>(i)), skel.bones[i].inverseBindMatrix);
  }
}

} // namespace burnhope
