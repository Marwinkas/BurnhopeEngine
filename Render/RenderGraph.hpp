#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>

namespace burnhope {

    // Описание одного этапа рендеринга
    struct RenderPassNode {
        std::string name;
        std::vector<VkImageMemoryBarrier> barriersBefore;
        std::function<void(VkCommandBuffer)> executeFunction;
    };

    class RenderGraph {
    public:
        // Добавляем новый этап в наш граф
        void addPass(const std::string& name, 
                     const std::vector<VkImageMemoryBarrier>& barriers, 
                     std::function<void(VkCommandBuffer)> execute) {
            passes.push_back({name, barriers, execute});
        }

        // Запускаем все этапы по очереди
        void execute(VkCommandBuffer commandBuffer) {
            for (const auto& pass : passes) {
                // Если есть барьеры, ставим их перед выполнением прохода
                if (!pass.barriersBefore.empty()) {
                    vkCmdPipelineBarrier(
                        commandBuffer,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, // Для начала используем широкие стадии
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        0, 
                        0, nullptr, 
                        0, nullptr,
                        static_cast<uint32_t>(pass.barriersBefore.size()), 
                        pass.barriersBefore.data()
                    );
                }

                // Запускаем само рисование или вычисления
                if (pass.executeFunction) {
                    pass.executeFunction(commandBuffer);
                }
            }
        }

        // Очищаем граф перед следующим кадром
        void clear() {
            passes.clear();
        }

    private:
        std::vector<RenderPassNode> passes;
    };

} // namespace burnhope