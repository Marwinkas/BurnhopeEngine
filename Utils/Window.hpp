#pragma once

#include "Core/Types.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <string>
#include <string_view>
#include <vulkan/vulkan.h>

namespace burnhope {

class BurnhopeWindow final : public NonCopyable {
public:
  BurnhopeWindow(int w, int h, std::string_view name);
  ~BurnhopeWindow();

  [[nodiscard]] bool shouldClose() const noexcept { return windowShouldClose_; }
  [[nodiscard]] VkExtent2D extent() const noexcept {
    return {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)};
  }
  [[nodiscard]] VkExtent2D getExtent() const noexcept { return extent(); }
  [[nodiscard]] bool wasWindowResized() const noexcept { return framebufferResized_; }
  void resetWindowResizedFlag() noexcept { framebufferResized_ = false; }
  [[nodiscard]] SDL_Window* sdlWindow() const noexcept { return window_; }
  [[nodiscard]] SDL_Window* getSDLWindow() const noexcept { return sdlWindow(); }

  void createWindowSurface(VkInstance instance, VkSurfaceKHR* surface);
  void pollEvents();

private:
  void initWindow();

  int width_{0};
  int height_{0};
  bool framebufferResized_{false};
  bool windowShouldClose_{false};
  std::string windowName_;
  SDL_Window* window_{nullptr};
};

} // namespace burnhope
