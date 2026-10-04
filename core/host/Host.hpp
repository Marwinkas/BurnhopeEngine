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
using HostSceneTickFn = bool (*)(void* user, const InputFrame& input, float dt);
using HostSceneRecordFn = void (*)(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame);
using HostReleaseFn = void (*)(void* user, Device& device);

// Приложение задаёт размер окна и функцию сборки холста. Кадр, swapchain и SDL живут здесь.
struct ViewDesc {
    const char* name = nullptr;
    WindowConfig window{};
    FrameDesc frame{};
    UiBuilder builder = nullptr;
    ColorBook* book = nullptr;
    HostOpsFn onOps = nullptr;
    HostCloseFn onClose = nullptr;
    HostSceneTickFn sceneTick = nullptr;
    HostSceneRecordFn sceneRecord = nullptr;
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
    HostSceneTickFn sceneTick = nullptr;
    HostSceneRecordFn sceneRecord = nullptr;
    void* user = nullptr;
};

struct Host {
    Device device;
    FrameArena arena;
    HostView view[kHostSlots]{};
    int primary = -1;
    int fileSlot = -1;
    bool ready = false;
    float clock = 0.0f;
    float frameDt = 0.0f;
    HostReleaseFn releaseGpu = nullptr;
    void* releaseUser = nullptr;
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
