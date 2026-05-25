#pragma once

#include <cstdint>

namespace burnhope {

/**
 * std430 / PhysicalStorageBuffer — must match shaders/common/SceneAddresses.slang
 * and gbuffer.mesh SPIR-V SceneAddresses_natural (ArrayStride 40, offsets 0/4/8/16/24/32/36).
 */
struct SceneAddresses {
  uint32_t globalUboHeap{0};
  /** First TexturePool entry heap slot after refreshBindlessSlots (frag: base + pool index). */
  uint32_t textureTableBase{0};
  uint64_t objectStorageBda{0};
  uint64_t materialStorageBda{0};
  uint64_t boneStorageBda{0};
  uint32_t defaultSampler{0};
  uint32_t textureTableCount{0};
  /** Frame GlobalUbo buffer device address (frag reads UBO via BDA; heap slot in globalUboHeap). */
  uint64_t globalUboBda{0};
};

static_assert(sizeof(SceneAddresses) == 48);
static_assert(offsetof(SceneAddresses, globalUboHeap) == 0);
static_assert(offsetof(SceneAddresses, textureTableBase) == 4);
static_assert(offsetof(SceneAddresses, objectStorageBda) == 8);
static_assert(offsetof(SceneAddresses, materialStorageBda) == 16);
static_assert(offsetof(SceneAddresses, boneStorageBda) == 24);
static_assert(offsetof(SceneAddresses, defaultSampler) == 32);
static_assert(offsetof(SceneAddresses, textureTableCount) == 36);
static_assert(offsetof(SceneAddresses, globalUboBda) == 40);

} // namespace burnhope
