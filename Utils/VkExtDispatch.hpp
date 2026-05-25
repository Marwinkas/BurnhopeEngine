#pragma once

#include <vulkan/vulkan.h>

namespace burnhope::vkext {

struct Dispatch final {
  PFN_vkGetPhysicalDeviceDescriptorSizeEXT getPhysicalDeviceDescriptorSizeEXT{nullptr};
  PFN_vkWriteResourceDescriptorsEXT writeResourceDescriptorsEXT{nullptr};
  PFN_vkWriteSamplerDescriptorsEXT writeSamplerDescriptorsEXT{nullptr};
  PFN_vkCmdBindResourceHeapEXT cmdBindResourceHeapEXT{nullptr};
  PFN_vkCmdBindSamplerHeapEXT cmdBindSamplerHeapEXT{nullptr};
  PFN_vkCreateShadersEXT createShadersEXT{nullptr};
  PFN_vkDestroyShaderEXT destroyShaderEXT{nullptr};
  PFN_vkCmdBindShadersEXT cmdBindShadersEXT{nullptr};
  PFN_vkCmdPushDataEXT cmdPushDataEXT{nullptr};
};

void loadInstance(VkInstance instance);
void loadDevice(VkDevice device);

[[nodiscard]] const Dispatch& get() noexcept;

} // namespace burnhope::vkext
