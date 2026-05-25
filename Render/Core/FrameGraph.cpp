#include "FrameGraph.hpp"

namespace burnhope {

FrameGraph::FrameGraph(BurnhopeDevice& device) : device_{device} {}

FrameGraph::~FrameGraph() = default;

void FrameGraph::addPass(
    std::string_view name,
    const std::vector<VkImageMemoryBarrier2>& barriers,
    std::function<void(VkCommandBuffer)> execute) {
  passes_.push_back({std::string{name}, barriers, std::move(execute)});
}

void FrameGraph::addPass(std::string_view name, std::function<void(VkCommandBuffer)> execute) {
  passes_.push_back({std::string{name}, {}, std::move(execute)});
}

void FrameGraph::execute(VkCommandBuffer commandBuffer) {
  for (const auto& pass : passes_) {
    if (!pass.barriersBefore.empty()) {
      VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
      dep.imageMemoryBarrierCount = static_cast<uint32_t>(pass.barriersBefore.size());
      dep.pImageMemoryBarriers = pass.barriersBefore.data();
      vkCmdPipelineBarrier2(commandBuffer, &dep);
    }
    if (pass.executeFunction) {
      pass.executeFunction(commandBuffer);
    }
  }
}

void FrameGraph::clear() noexcept {
  passes_.clear();
}

} // namespace burnhope
