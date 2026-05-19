#include "Window.hpp"
#include <imgui_impl_sdl3.h> // Include ImGui SDL3 backend header
#include <stdexcept>
namespace burnhope
{
  BurnhopeWindow::BurnhopeWindow(int w, int h, std::string name) : width{w}, height{h}, windowName{name}
  {
    initWindow();
  }
  BurnhopeWindow::~BurnhopeWindow()
  {
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
  void BurnhopeWindow::initWindow()
  {
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
      throw std::runtime_error("Failed to initialize SDL");
    }
    
    window = SDL_CreateWindow(
      windowName.c_str(),
      width,
      height,
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window)
    {
      throw std::runtime_error("Failed to create SDL window");
    }
  }
  void BurnhopeWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface)
  {
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, surface))
    {
      throw std::runtime_error("failed to create window surface");
    }
  }
  void BurnhopeWindow::pollEvents()
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
      {
        windowShouldClose = true;
      }
      else if (event.type == SDL_EVENT_WINDOW_RESIZED)
      {
        framebufferResized = true;
        width = event.window.data1;
        height = event.window.data2;
      }
      ImGui_ImplSDL3_ProcessEvent(&event); // Forward all events to ImGui
    }
  }
}
