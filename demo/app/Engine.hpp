#pragma once

#include "gpu_scene/MeshletGpu.hpp"
#include "memory/FrameArena.hpp"
#include "platform/Window.hpp"
#include "render/SSR.hpp"
#include "render/Shade.hpp"
#include "render/Tonemap.hpp"
#include "render/Visbuffer.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"

namespace burnhope {

struct Engine {
    Window window;
    Device device;
    Swapchain swap;
    DescriptorHeaps heaps;
    MeshletGpu scene;
    VisPass vis;
    ShadePass shade;
    SsrPass ssr;
    TonemapPass tonemap;
    FrameArena arena;
    bool classicIndirect = false;
};

[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
