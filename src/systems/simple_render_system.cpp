#include "simple_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <cassert>
#include <stdexcept>

namespace burnhope {

struct SimplePushConstantData {
  glm::mat4 modelMatrix{1.f}; //матрица трансформации объекта.
  glm::mat4 normalMatrix{1.f}; //используется для корректного преобразования нормалей при освещении.
};

SimpleRenderSystem::SimpleRenderSystem(
    BurnhopeDevice& device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
    : lveDevice{device} {
  createPipelineLayout(globalSetLayout);//создает layout для пайплайна (включает descriptor set и push-константы).
  createPipeline(renderPass);//создает сам графический пайплайн.
}

SimpleRenderSystem::~SimpleRenderSystem() {
  vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

void SimpleRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(SimplePushConstantData); //Описание push-констант: для каких шейдеров и сколько байт.

  renderSystemLayout =
      BurnhopeDescriptorSetLayout::Builder(lveDevice)
          // Uniform Buffer (например, матрицы или свойства материала).
          .addBinding(0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          //Image Sampler (например, текстура).
          .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{//Список дескрипторных layout'ов:
      globalSetLayout,//общий 
      renderSystemLayout->getDescriptorSetLayout()};//конкретно для моделей (текстура и буфер)

  //создание пайплайна
  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create pipeline layout!");
  }
}

void SimpleRenderSystem::createPipeline(VkRenderPass renderPass) {
  assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  BurnhopePipeline::defaultPipelineConfigInfo(pipelineConfig);
  pipelineConfig.renderPass = renderPass;
  pipelineConfig.pipelineLayout = pipelineLayout;
  lvePipeline = std::make_unique<BurnhopePipeline>(
      lveDevice,
      "shaders/simple_shader.vert.spv",
      "shaders/simple_shader.frag.spv",
      pipelineConfig);
}

void SimpleRenderSystem::renderGameObjects(FrameInfo& frameInfo) {
  lvePipeline->bind(frameInfo.commandBuffer);//Привязка пайплайна
  // Привязка глобального дескриптора
  vkCmdBindDescriptorSets(
      frameInfo.commandBuffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipelineLayout,
      0,
      1,
      &frameInfo.globalDescriptorSet,
      0,
      nullptr);
  
  for (auto& kv : frameInfo.gameObjects) {
    auto& obj = kv.second;

    if (obj.model == nullptr) continue;

    auto bufferInfo = obj.getBufferInfo(frameInfo.frameIndex);
    auto imageInfo = obj.material->diffuseMap->getImageInfo();
    auto normalInfo = obj.material->normalMap->getImageInfo();
    auto AOMap = obj.material->AOMap->getImageInfo();
    auto RoughnessMap = obj.material->RoughnessMap->getImageInfo();
    auto MetallicMap = obj.material->MetallicMap->getImageInfo();

    auto shadowMapInfo = obj.material->MetallicMap->getImageInfo();//исправить на тени в будущем

    VkDescriptorSet gameObjectDescriptorSet;


    BurnhopeDescriptorWriter(*renderSystemLayout, frameInfo.frameDescriptorPool)
        .writeBuffer(0, &bufferInfo)
        .writeImage(1, &imageInfo)
        .writeImage(2, &normalInfo)
        .writeImage(3, &AOMap)
        .writeImage(4, &RoughnessMap)
        .writeImage(5, &MetallicMap)
        .writeImage(6, &shadowMapInfo)
        .build(gameObjectDescriptorSet);

    vkCmdBindDescriptorSets(
        frameInfo.commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout,
        1,  // starting set (0 is the globalDescriptorSet, 1 is the set specific to this system)
        1,  // set count
        &gameObjectDescriptorSet,
        0,
        nullptr);

    SimplePushConstantData push{};
    push.modelMatrix = obj.transform.mat4();
    push.normalMatrix = obj.transform.normalMatrix();

    vkCmdPushConstants(
        frameInfo.commandBuffer,
        pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(SimplePushConstantData),
        &push);
    obj.model->bind(frameInfo.commandBuffer);
    obj.model->draw(frameInfo.commandBuffer);
  }
}

}  // namespace burnhope
