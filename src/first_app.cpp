#include "first_app.hpp"

#include "keyboard_movement_controller.hpp"
#include "lve_buffer.hpp"
#include "lve_camera.hpp"
#include "systems/point_light_system.hpp"
#include "systems/simple_render_system.hpp"

// libs
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

// std
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace burnhope {

FirstApp::FirstApp() {
  globalPool =
      BurnhopeDescriptorPool::Builder(lveDevice)
          .setMaxSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT)
          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT)
          .build();

  // build frame descriptor pools
  framePools.resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
  auto framePoolBuilder = BurnhopeDescriptorPool::Builder(lveDevice)
                              .setMaxSets(1000)
                              .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000)
                              .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000)
                              .setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
  for (int i = 0; i < framePools.size(); i++) {
    framePools[i] = framePoolBuilder.build();
  }

  loadGameObjects();
}

FirstApp::~FirstApp() {}

void FirstApp::run() {
  std::vector<std::unique_ptr<BurnhopeBuffer>> uboBuffers(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < uboBuffers.size(); i++) {
    uboBuffers[i] = std::make_unique<BurnhopeBuffer>(
        lveDevice,
        sizeof(GlobalUbo),
        1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    uboBuffers[i]->map();
  }

  auto globalSetLayout =
      BurnhopeDescriptorSetLayout::Builder(lveDevice)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
          .build();

  std::vector<VkDescriptorSet> globalDescriptorSets(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < globalDescriptorSets.size(); i++) {
    auto bufferInfo = uboBuffers[i]->descriptorInfo();
    BurnhopeDescriptorWriter(*globalSetLayout, *globalPool)
        .writeBuffer(0, &bufferInfo)
        .build(globalDescriptorSets[i]);
  }

  std::cout << "Alignment: " << lveDevice.properties.limits.minUniformBufferOffsetAlignment << "\n";
  std::cout << "atom size: " << lveDevice.properties.limits.nonCoherentAtomSize << "\n";

  SimpleRenderSystem simpleRenderSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout()};
  PointLightSystem pointLightSystem{
      lveDevice,
      lveRenderer.getSwapChainRenderPass(),
      globalSetLayout->getDescriptorSetLayout()};
  BurnhopeCamera camera{};

  auto& viewerObject = gameObjectManager.createGameObject();
  viewerObject.transform.translation.z = -2.5f;
  KeyboardMovementController cameraController{};

  auto currentTime = std::chrono::high_resolution_clock::now();
  // Переменные для FPS
  int frameCount = 0;
  auto fpsTimer = currentTime;

  while (!lveWindow.shouldClose()) {
    glfwPollEvents();

    auto newTime = std::chrono::high_resolution_clock::now();
    float frameTime =
        std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
    currentTime = newTime;

    frameCount++;
    float timeSinceLastFpsUpdate = std::chrono::duration<float>(newTime - fpsTimer).count();
    if (timeSinceLastFpsUpdate >= 1.0f) {
      std::cout << "FPS: " << frameCount << std::endl;
      frameCount = 0;
      fpsTimer = newTime;
    }

    cameraController.moveInPlaneXZ(lveWindow.getGLFWwindow(), frameTime, viewerObject);
    camera.setViewYXZ(viewerObject.transform.translation, viewerObject.transform.rotation);

    float aspect = lveRenderer.getAspectRatio();
    camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f);

    if (auto commandBuffer = lveRenderer.beginFrame()) {
      int frameIndex = lveRenderer.getFrameIndex();
      framePools[frameIndex]->resetPool();
      FrameInfo frameInfo{
          frameIndex,
          frameTime,
          commandBuffer,
          camera,
          globalDescriptorSets[frameIndex],
          *framePools[frameIndex],
          gameObjectManager.gameObjects};

      // update
      GlobalUbo ubo{};
      ubo.projection = camera.getProjection();
      ubo.view = camera.getView();
      ubo.inverseView = camera.getInverseView();
      pointLightSystem.update(frameInfo, ubo);
      uboBuffers[frameIndex]->writeToBuffer(&ubo);
      uboBuffers[frameIndex]->flush();

      // final step of update is updating the game objects buffer data
      // The render functions MUST not change a game objects transform data
      gameObjectManager.updateBuffer(frameIndex);

      // render
      lveRenderer.beginSwapChainRenderPass(commandBuffer);

      // order here matters
      simpleRenderSystem.renderGameObjects(frameInfo);
      pointLightSystem.render(frameInfo);

      lveRenderer.endSwapChainRenderPass(commandBuffer);
      lveRenderer.endFrame();
    }
  }

  vkDeviceWaitIdle(lveDevice.device());
}
int test = 0;



void FirstApp::loadGameObjects() {
  std::shared_ptr<BurnhopeTexture> diffuseTexture =
      BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/diffuse2.png");
  std::shared_ptr<BurnhopeTexture> normalTexture =
      BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/normal2.png");
  std::shared_ptr<BurnhopeTexture> aoTexture =
      BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/ao2.png");
  std::shared_ptr<BurnhopeTexture> metallicTexture =
      BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/metallic2.png");
  std::shared_ptr<BurnhopeTexture> rougnessTexture =
      BurnhopeTexture::createTextureFromFile(lveDevice, "../textures/rougness2.png");

  std::shared_ptr<BurnhopeModel> lveModel =
      BurnhopeModel::createModelFromFile(lveDevice, "models/cube.obj");

  std::shared_ptr<Material> material = std::make_shared<Material>();
  material->diffuseMap = diffuseTexture;
  material->normalMap = normalTexture;
  material->AOMap = aoTexture;
  material->MetallicMap = metallicTexture;
  material->RoughnessMap = rougnessTexture;

  auto& flatVase = gameObjectManager.createGameObject();

  flatVase.model = lveModel;
  flatVase.material = material;
  flatVase.transform.translation = {-.5f, .5f, 0.f};
  flatVase.transform.scale = {0.5f, 0.5f, 0.5f};

  lveModel = BurnhopeModel::createModelFromFile(lveDevice, "models/smooth_vase.obj");
  auto& smoothVase = gameObjectManager.createGameObject();
  smoothVase.model = lveModel;
  smoothVase.material = material;
  smoothVase.transform.translation = {.5f, .5f, 0.f};
  smoothVase.transform.scale = {3.f, 1.5f, 3.f};


  std::vector<glm::vec3> lightColors{

      {1.f, 1.f, 1.f}  //
  };

  for (int i = 0; i < lightColors.size(); i++) {
    auto& pointLight = gameObjectManager.makePointLight(10.0f);
    pointLight.color = lightColors[i];
    auto rotateLight = glm::rotate(
        glm::mat4(1.f),
        (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, -1.f, 0.f});
    pointLight.transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
  }
}
}  // namespace burnhope
