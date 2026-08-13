#include "Window.hpp"
#include <stdexcept>

namespace burnhope
{
  BurnhopeWindow::BurnhopeWindow(int w, int h, std::string name) : width{w}, height{h}, windowName{std::move(name)}
  {
    initWindow();
  }

  BurnhopeWindow::~BurnhopeWindow()
  {
    if (window != nullptr)
    {
      SDL_DestroyWindow(window);
      window = nullptr;
    }
    SDL_Quit();
  }

  void BurnhopeWindow::initWindow()
  {
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
      throw std::runtime_error(std::string("failed to initialize SDL3: ") + SDL_GetError());
    }

    window = SDL_CreateWindow(windowName.c_str(), width, height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr)
    {
      SDL_Quit();
      throw std::runtime_error(std::string("failed to create SDL3 window: ") + SDL_GetError());
    }

    updateExtentFromWindow();
  }

  void BurnhopeWindow::updateExtentFromWindow()
  {
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (!SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight))
    {
      throw std::runtime_error(std::string("failed to query SDL3 window size: ") + SDL_GetError());
    }
    width = pixelWidth;
    height = pixelHeight;
  }

  VkExtent2D BurnhopeWindow::getExtent()
  {
    updateExtentFromWindow();
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
  }

  bool BurnhopeWindow::popEvent(SDL_Event &out)
  {
    return SDL_PollEvent(&out);
  }

  void BurnhopeWindow::handleSDLEvent(const SDL_Event &event)
  {
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
      closeRequested = true;
      break;
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      if (event.window.windowID == SDL_GetWindowID(window))
      {
        closeRequested = true;
      }
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      if (event.window.windowID == SDL_GetWindowID(window))
      {
        framebufferResized = true;
        width = event.window.data1;
        height = event.window.data2;
      }
      break;
    default:
      break;
    }
  }

  void BurnhopeWindow::waitForEvents()
  {
    SDL_Event event{};
    if (SDL_WaitEvent(&event))
    {
      handleSDLEvent(event);
      while (popEvent(event))
      {
        handleSDLEvent(event);
      }
    }
  }

  void BurnhopeWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface)
  {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, surface))
    {
      throw std::runtime_error(std::string("failed to create Vulkan window surface: ") + SDL_GetError());
    }
  }
}
