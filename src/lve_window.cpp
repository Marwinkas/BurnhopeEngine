#include "lve_window.hpp"

// std
#include <stdexcept>

namespace burnhope {

BurnhopeWindow::BurnhopeWindow(int w, int h, std::string name) : width{w}, height{h}, windowName{name} {
  initWindow();
}

BurnhopeWindow::~BurnhopeWindow() {
  glfwDestroyWindow(window);
  glfwTerminate();
}

void BurnhopeWindow::initWindow() {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = glfwCreateWindow(width, height, windowName.c_str(), nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void BurnhopeWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) {
  if (glfwCreateWindowSurface(instance, window, nullptr, surface) != VK_SUCCESS) {
    throw std::runtime_error("failed to craete window surface");
  }
}

void BurnhopeWindow::framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto lveWindow = reinterpret_cast<BurnhopeWindow *>(glfwGetWindowUserPointer(window));
  lveWindow->framebufferResized = true;
  lveWindow->width = width;
  lveWindow->height = height;
}

}  // namespace burnhope
