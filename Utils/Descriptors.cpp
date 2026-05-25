#include "Descriptors.hpp"
#include "Core/Types.hpp"
#include <cassert>

namespace burnhope {

BurnhopeDescriptorSetLayout::Builder& BurnhopeDescriptorSetLayout::Builder::addBinding(
    uint32_t binding,
    VkDescriptorType type,
    VkShaderStageFlags stages,
    uint32_t count) {
  assert(bindings_.count(binding) == 0);
  VkDescriptorSetLayoutBinding layoutBinding{};
  layoutBinding.binding = binding;
  layoutBinding.descriptorType = type;
  layoutBinding.descriptorCount = count;
  layoutBinding.stageFlags = stages;
  bindings_[binding] = layoutBinding;
  return *this;
}

std::unique_ptr<BurnhopeDescriptorSetLayout> BurnhopeDescriptorSetLayout::Builder::build() const {
  return std::make_unique<BurnhopeDescriptorSetLayout>(device_, bindings_);
}

BurnhopeDescriptorSetLayout::BurnhopeDescriptorSetLayout(
    BurnhopeDevice& device,
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
    : device_{device}, bindings_{std::move(bindings)} {
  std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
  layoutBindings.reserve(bindings_.size());
  for (const auto& kv : bindings_) {
    layoutBindings.push_back(kv.second);
  }

  VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = static_cast<uint32_t>(layoutBindings.size());
  info.pBindings = layoutBindings.data();
  if (vkCreateDescriptorSetLayout(device_.device(), &info, nullptr, &layout_) != VK_SUCCESS) {
    throwVkError("vkCreateDescriptorSetLayout");
  }
}

BurnhopeDescriptorSetLayout::~BurnhopeDescriptorSetLayout() {
  vkDestroyDescriptorSetLayout(device_.device(), layout_, nullptr);
}

BurnhopeDescriptorPool::Builder& BurnhopeDescriptorPool::Builder::addPoolSize(
    VkDescriptorType type,
    uint32_t count) {
  poolSizes_.push_back({type, count});
  return *this;
}

BurnhopeDescriptorPool::Builder& BurnhopeDescriptorPool::Builder::setPoolFlags(
    VkDescriptorPoolCreateFlags flags) {
  flags_ = flags;
  return *this;
}

BurnhopeDescriptorPool::Builder& BurnhopeDescriptorPool::Builder::setMaxSets(uint32_t count) {
  maxSets_ = count;
  return *this;
}

std::unique_ptr<BurnhopeDescriptorPool> BurnhopeDescriptorPool::Builder::build() const {
  return std::make_unique<BurnhopeDescriptorPool>(device_, maxSets_, flags_, poolSizes_);
}

BurnhopeDescriptorPool::BurnhopeDescriptorPool(
    BurnhopeDevice& device,
    uint32_t maxSets,
    VkDescriptorPoolCreateFlags flags,
    std::vector<VkDescriptorPoolSize> poolSizes)
    : device_{device} {
  VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  info.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  info.pPoolSizes = poolSizes.data();
  info.maxSets = maxSets;
  info.flags = flags;
  if (vkCreateDescriptorPool(device_.device(), &info, nullptr, &pool_) != VK_SUCCESS) {
    throwVkError("vkCreateDescriptorPool");
  }
}

BurnhopeDescriptorPool::~BurnhopeDescriptorPool() {
  vkDestroyDescriptorPool(device_.device(), pool_, nullptr);
}

bool BurnhopeDescriptorPool::allocateDescriptor(
    VkDescriptorSetLayout layout,
    VkDescriptorSet& out) const {
  VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  alloc.descriptorPool = pool_;
  alloc.pSetLayouts = &layout;
  alloc.descriptorSetCount = 1;
  return vkAllocateDescriptorSets(device_.device(), &alloc, &out) == VK_SUCCESS;
}

void BurnhopeDescriptorPool::freeDescriptors(std::vector<VkDescriptorSet>& sets) const {
  vkFreeDescriptorSets(
      device_.device(),
      pool_,
      static_cast<uint32_t>(sets.size()),
      sets.data());
}

void BurnhopeDescriptorPool::resetPool() {
  vkResetDescriptorPool(device_.device(), pool_, 0);
}

BurnhopeDescriptorWriter::BurnhopeDescriptorWriter(
    BurnhopeDescriptorSetLayout& layout,
    BurnhopeDescriptorPool& pool)
    : layout_{layout}, pool_{pool} {}

BurnhopeDescriptorWriter& BurnhopeDescriptorWriter::writeBuffer(
    uint32_t binding,
    const VkDescriptorBufferInfo* info) {
  assert(layout_.bindings_.count(binding) == 1);
  const auto& bindingDesc = layout_.bindings_[binding];
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.descriptorType = bindingDesc.descriptorType;
  write.dstBinding = binding;
  write.pBufferInfo = info;
  write.descriptorCount = 1;
  writes_.push_back(write);
  return *this;
}

BurnhopeDescriptorWriter& BurnhopeDescriptorWriter::writeImage(
    uint32_t binding,
    const VkDescriptorImageInfo* info) {
  assert(layout_.bindings_.count(binding) == 1);
  const auto& bindingDesc = layout_.bindings_[binding];
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.descriptorType = bindingDesc.descriptorType;
  write.dstBinding = binding;
  write.pImageInfo = info;
  write.descriptorCount = 1;
  writes_.push_back(write);
  return *this;
}

BurnhopeDescriptorWriter& BurnhopeDescriptorWriter::writeImageArray(
    uint32_t binding,
    const std::vector<VkDescriptorImageInfo>& infos) {
  assert(layout_.bindings_.count(binding) == 1);
  const auto& bindingDesc = layout_.bindings_[binding];
  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.descriptorType = bindingDesc.descriptorType;
  write.dstBinding = binding;
  write.pImageInfo = infos.data();
  write.descriptorCount = static_cast<uint32_t>(infos.size());
  writes_.push_back(write);
  return *this;
}

bool BurnhopeDescriptorWriter::build(VkDescriptorSet& set) {
  if (!pool_.allocateDescriptor(layout_.handle(), set)) {
    return false;
  }
  overwrite(set);
  return true;
}

void BurnhopeDescriptorWriter::overwrite(VkDescriptorSet& set) {
  for (auto& write : writes_) {
    write.dstSet = set;
  }
  vkUpdateDescriptorSets(pool_.device_.device(), writes_.size(), writes_.data(), 0, nullptr);
}

} // namespace burnhope
