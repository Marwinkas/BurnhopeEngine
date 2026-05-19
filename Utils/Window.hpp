#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <vulkan/vulkan.h>
namespace burnhope
{
  class BurnhopeWindow
  {
  public:
    BurnhopeWindow(int w, int h, std::string name);
    ~BurnhopeWindow();
    BurnhopeWindow(const BurnhopeWindow &) = delete;
    BurnhopeWindow &operator=(const BurnhopeWindow &) = delete;
    bool shouldClose() { return windowShouldClose; }
    VkExtent2D getExtent() { return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}; }
    bool wasWindowResized() { return framebufferResized; }
    void resetWindowResizedFlag() { framebufferResized = false; }
    SDL_Window *getSDLWindow() const { return window; }
    void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface);
    void pollEvents();
  private:
    void initWindow();
    int width;
    int height;
    bool framebufferResized = false;
    bool windowShouldClose = false;
    std::string windowName;
    SDL_Window *window;
  };
}
