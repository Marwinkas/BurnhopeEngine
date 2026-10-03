#pragma once

#include "memory/FrameArena.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/Canvas.hpp"

namespace burnhope {

constexpr int kHostSlots = 8;

struct Host;

using HostOpsFn = void (*)(Host& host, int slot, const WindowOps& ops, void* user);
using HostCloseFn = void (*)(Host& host, int slot, void* user);

// Приложение задаёт размер окна и функцию сборки холста. Кадр, swapchain и SDL живут здесь.
struct ViewDesc {
    const char* name = nullptr;
    WindowConfig window{};
    FrameDesc frame{};
    UiBuilder builder = nullptr;
    ColorBook* book = nullptr;
    HostOpsFn onOps = nullptr;
    HostCloseFn onClose = nullptr;
    void* user = nullptr;
};

struct HostView {
    bool on = false;
    char name[24]{};
    FrameDesc frame{};
    Window window{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    Swapchain swap{};
    DescriptorHeaps heaps{};
    Canvas canvas{};
    ColorBook* book = nullptr;
    HostOpsFn onOps = nullptr;
    HostCloseFn onClose = nullptr;
    void* user = nullptr;
};

struct Host {
    Device device;
    FrameArena arena;
    HostView view[kHostSlots]{};
    int primary = -1;
    int fileSlot = -1;
    bool ready = false;
};

[[nodiscard]] bool hostInit(Host& host);
void hostShutdown(Host& host);
// То же имя поднимает уже открытое окно. Новое окно — первый свободный слот.
[[nodiscard]] int hostOpen(Host& host, const ViewDesc& desc);
void hostClose(Host& host, int slot);
[[nodiscard]] bool hostSetFrame(Host& host, int slot, const FrameDesc& frame);
[[nodiscard]] FrameDesc hostFrame(const Host& host, int slot);
[[nodiscard]] bool hostTick(Host& host);
[[nodiscard]] bool hostQuit(const Host& host);
[[nodiscard]] Canvas* hostCanvas(Host& host, int slot);

} // namespace burnhope
