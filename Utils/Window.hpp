#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <string>

namespace burnhope
{
  class BurnhopeWindow
  {
  public:
    BurnhopeWindow(int w, int h, std::string name);
    ~BurnhopeWindow();
    BurnhopeWindow(const BurnhopeWindow &) = delete;
    BurnhopeWindow &operator=(const BurnhopeWindow &) = delete;

    bool shouldClose() const { return closeRequested; }
    VkExtent2D getExtent();
    bool wasWindowResized() { return framebufferResized; }
    void resetWindowResizedFlag() { framebufferResized = false; }
    SDL_Window *getSDLWindow() const { return window; }

    bool popEvent(SDL_Event &out);
    void handleSDLEvent(const SDL_Event &event);
    void waitForEvents();

    void createWindowSurface(VkInstance instance, VkSurfaceKHR *surface);

  private:
    void initWindow();
    void updateExtentFromWindow();

    int width;
    int height;
    bool framebufferResized = false;
    bool closeRequested = false;
    std::string windowName;
    SDL_Window *window = nullptr;
  };
}
