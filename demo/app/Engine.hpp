#pragma once

#include "memory/FrameArena.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/Canvas.hpp"

namespace burnhope {

struct Engine {
    Window window;
    Window tool;
    VkSurfaceKHR toolSurface = VK_NULL_HANDLE;
    Device device;
    Swapchain swap;
    Swapchain toolSwap;
    DescriptorHeaps heaps;
    DescriptorHeaps toolHeaps;
    Canvas canvas;
    Canvas toolCanvas;
    Window colorWin;
    VkSurfaceKHR colorSurface = VK_NULL_HANDLE;
    Swapchain colorSwap;
    DescriptorHeaps colorHeaps;
    Canvas colorCanvas;
    ColorBook colors{};
    FrameArena arena;
    bool toolOn = false;
    bool colorOn = false;
};

[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
