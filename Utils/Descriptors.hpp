#pragma once

#include "Device.hpp"
#include "ResourceHeap.hpp"
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

/**
 * Transitional VkDescriptorSet helpers for shader-object set layouts.
 * New bindless data: ResourceHeap (always bind per frame).
 * Migrate call sites to heap offsets + push constants; do not add new pools.
 */
class BurnhopeDescriptorSetLayout final : public NonCopyable {
public:
  class Builder {
  public:
    explicit Builder(BurnhopeDevice& device) : device_{device} {}
    Builder& addBinding(
        uint32_t binding,
        VkDescriptorType type,
        VkShaderStageFlags stages,
        uint32_t count = 1);
    [[nodiscard]] std::unique_ptr<BurnhopeDescriptorSetLayout> build() const;

  private:
    BurnhopeDevice& device_;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings_{};
  };

  BurnhopeDescriptorSetLayout(
      BurnhopeDevice& device,
      std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);
  ~BurnhopeDescriptorSetLayout();

  [[nodiscard]] VkDescriptorSetLayout handle() const noexcept { return layout_; }
  [[nodiscard]] VkDescriptorSetLayout getDescriptorSetLayout() const noexcept { return handle(); }

private:
  BurnhopeDevice& device_;
  VkDescriptorSetLayout layout_{VK_NULL_HANDLE};
  std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings_;
  friend class BurnhopeDescriptorWriter;
};

class BurnhopeDescriptorPool final : public NonCopyable {
public:
  class Builder {
  public:
    explicit Builder(BurnhopeDevice& device) : device_{device} {}
    Builder& addPoolSize(VkDescriptorType type, uint32_t count);
    Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
    Builder& setMaxSets(uint32_t count);
    [[nodiscard]] std::unique_ptr<BurnhopeDescriptorPool> build() const;

  private:
    BurnhopeDevice& device_;
    std::vector<VkDescriptorPoolSize> poolSizes_{};
    uint32_t maxSets_{1000};
    VkDescriptorPoolCreateFlags flags_{0};
  };

  BurnhopeDescriptorPool(
      BurnhopeDevice& device,
      uint32_t maxSets,
      VkDescriptorPoolCreateFlags flags,
      std::vector<VkDescriptorPoolSize> poolSizes);
  ~BurnhopeDescriptorPool();

  [[nodiscard]] bool allocateDescriptor(VkDescriptorSetLayout layout, VkDescriptorSet& out) const;
  void freeDescriptors(std::vector<VkDescriptorSet>& sets) const;
  void resetPool();

private:
  BurnhopeDevice& device_;
  VkDescriptorPool pool_{VK_NULL_HANDLE};
  friend class BurnhopeDescriptorWriter;
};

class BurnhopeDescriptorWriter final {
public:
  BurnhopeDescriptorWriter(BurnhopeDescriptorSetLayout& layout, BurnhopeDescriptorPool& pool);

  BurnhopeDescriptorWriter& writeBuffer(uint32_t binding, const VkDescriptorBufferInfo* info);
  BurnhopeDescriptorWriter& writeImage(uint32_t binding, const VkDescriptorImageInfo* info);
  BurnhopeDescriptorWriter& writeImageArray(
      uint32_t binding,
      const std::vector<VkDescriptorImageInfo>& infos);

  [[nodiscard]] bool build(VkDescriptorSet& set);
  void overwrite(VkDescriptorSet& set);

private:
  BurnhopeDescriptorSetLayout& layout_;
  BurnhopeDescriptorPool& pool_;
  std::vector<VkWriteDescriptorSet> writes_{};
};

} // namespace burnhope
