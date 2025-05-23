#pragma once
#include "Material.hpp"
#include "lve_descriptors.hpp"
#include "lve_device.hpp"
#include "lve_game_object.hpp"
#include "lve_renderer.hpp"
#include "lve_window.hpp"

// std
#include <memory>
#include <vector>

namespace burnhope {
class FirstApp {
 public:
  static constexpr int WIDTH = 800;
  static constexpr int HEIGHT = 600;

  FirstApp();
  ~FirstApp();

  FirstApp(const FirstApp &) = delete;
  FirstApp &operator=(const FirstApp &) = delete;

  void run();

 private:
  void loadGameObjects();

  BurnhopeWindow lveWindow{WIDTH, HEIGHT, "Vulkan Tutorial"};
  BurnhopeDevice lveDevice{lveWindow};
  BurnhopeRenderer lveRenderer{lveWindow, lveDevice};

  // note: order of declarations matters
  std::unique_ptr<BurnhopeDescriptorPool> globalPool{};
  std::vector<std::unique_ptr<BurnhopeDescriptorPool>> framePools;
  BurnhopeGameObjectManager gameObjectManager{lveDevice};
};
}  // namespace burnhope
