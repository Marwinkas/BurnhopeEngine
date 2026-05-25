#include "Window.hpp"
#include "Core/Types.hpp"
#include <imgui_impl_sdl3.h>

namespace burnhope {

BurnhopeWindow::BurnhopeWindow(int w, int h, std::string_view name)
    : width_{w}, height_{h}, windowName_{name} {
  initWindow();
}

BurnhopeWindow::~BurnhopeWindow() {
  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

void BurnhopeWindow::initWindow() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    throwVkError("SDL_Init video failed");
  }

  window_ = SDL_CreateWindow(
      windowName_.c_str(),
      width_,
      height_,
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (!window_) {
    throwVkError("SDL_CreateWindow failed");
  }
}

void BurnhopeWindow::createWindowSurface(VkInstance instance, VkSurfaceKHR* surface) {
  if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, surface)) {
    throwVkError("SDL_Vulkan_CreateSurface failed");
  }
}

void BurnhopeWindow::pollEvents() {
  SDL_Event event{};
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) {
      windowShouldClose_ = true;
    } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
      framebufferResized_ = true;
      width_ = event.window.data1;
      height_ = event.window.data2;
    }
    ImGui_ImplSDL3_ProcessEvent(&event);
  }
}

} // namespace burnhope
