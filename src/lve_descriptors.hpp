#pragma once

#include "lve_device.hpp"

// std
#include <memory>
#include <unordered_map>
#include <vector>

namespace burnhope {

class BurnhopeDescriptorSetLayout {
 public:
  class Builder {
   public:
    Builder(BurnhopeDevice &lveDevice) : lveDevice{lveDevice} {}

    Builder &addBinding(
        uint32_t binding,
        VkDescriptorType descriptorType,
        VkShaderStageFlags stageFlags,
        uint32_t count = 1);
    std::unique_ptr<BurnhopeDescriptorSetLayout> build() const;

   private:
    BurnhopeDevice &lveDevice;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings{};
  };

  BurnhopeDescriptorSetLayout(
      BurnhopeDevice &lveDevice, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);
  ~BurnhopeDescriptorSetLayout();
  BurnhopeDescriptorSetLayout(const BurnhopeDescriptorSetLayout &) = delete;
  BurnhopeDescriptorSetLayout &operator=(const BurnhopeDescriptorSetLayout &) = delete;

  VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

 private:
  BurnhopeDevice &lveDevice;
  VkDescriptorSetLayout descriptorSetLayout;
  std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings;

  friend class BurnhopeDescriptorWriter;
};

class BurnhopeDescriptorPool {
 public:
  class Builder {
   public:
    Builder(BurnhopeDevice &lveDevice) : lveDevice{lveDevice} {}

    Builder &addPoolSize(VkDescriptorType descriptorType, uint32_t count);
    Builder &setPoolFlags(VkDescriptorPoolCreateFlags flags);
    Builder &setMaxSets(uint32_t count);
    std::unique_ptr<BurnhopeDescriptorPool> build() const;

   private:
    BurnhopeDevice &lveDevice;
    std::vector<VkDescriptorPoolSize> poolSizes{};
    uint32_t maxSets = 1000;
    VkDescriptorPoolCreateFlags poolFlags = 0;
  };

  BurnhopeDescriptorPool(
      BurnhopeDevice &lveDevice,
      uint32_t maxSets,
      VkDescriptorPoolCreateFlags poolFlags,
      const std::vector<VkDescriptorPoolSize> &poolSizes);
  ~BurnhopeDescriptorPool();
  BurnhopeDescriptorPool(const BurnhopeDescriptorPool &) = delete;
  BurnhopeDescriptorPool &operator=(const BurnhopeDescriptorPool &) = delete;

  bool allocateDescriptor(
      const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet &descriptor) const;

  void freeDescriptors(std::vector<VkDescriptorSet> &descriptors) const;

  void resetPool();

 private:
  BurnhopeDevice &lveDevice;
  VkDescriptorPool descriptorPool;

  friend class BurnhopeDescriptorWriter;
};

class BurnhopeDescriptorWriter {
 public:
  BurnhopeDescriptorWriter(BurnhopeDescriptorSetLayout &setLayout, BurnhopeDescriptorPool &pool);

  BurnhopeDescriptorWriter &writeBuffer(uint32_t binding, VkDescriptorBufferInfo *bufferInfo);
  BurnhopeDescriptorWriter &writeImage(uint32_t binding, VkDescriptorImageInfo *imageInfo);

  bool build(VkDescriptorSet &set);
  void overwrite(VkDescriptorSet &set);

 private:
  BurnhopeDescriptorSetLayout &setLayout;
  BurnhopeDescriptorPool &pool;
  std::vector<VkWriteDescriptorSet> writes;
};

}  // namespace burnhope
