#pragma once

#include "lve_camera.hpp"
#include "lve_device.hpp"
#include "lve_frame_info.hpp"
#include "lve_game_object.hpp"
#include "lve_pipeline.hpp"

// std
#include <memory>
#include <vector>

namespace burnhope {
class SimpleRenderSystem {
 public:
  SimpleRenderSystem(
      BurnhopeDevice &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout);
  ~SimpleRenderSystem();

  SimpleRenderSystem(const SimpleRenderSystem &) = delete;
  SimpleRenderSystem &operator=(const SimpleRenderSystem &) = delete;

  void renderGameObjects(FrameInfo &frameInfo);

 private:
  void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
  void createPipeline(VkRenderPass renderPass);

  BurnhopeDevice &lveDevice;

  std::unique_ptr<BurnhopePipeline> lvePipeline;
  VkPipelineLayout pipelineLayout;

  std::unique_ptr<BurnhopeDescriptorSetLayout> renderSystemLayout;
};
}  // namespace burnhope
