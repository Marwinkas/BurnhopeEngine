#pragma once

#include "memory/FrameArena.hpp"
#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Device.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/rhi/Canvas.hpp"

namespace burnhope {

struct ColorBook;

constexpr int kHostSlots = 8;

constexpr HeapLayout kDefaultHostHeap{.buffers = 64, .images = 810, .samplers = 3};

struct Host;

using HostOpsFn = void (*)(Host& host, int slot, const WindowOps& ops, void* user);
using HostCloseFn = void (*)(Host& host, int slot, void* user);
using HostDropFn = void (*)(Host& host, int slot, const char* path, void* user);
using HostSceneTickFn = bool (*)(void* user, const InputFrame& input, float dt);
using HostSceneRecordFn = void (*)(void* user, VkCommandBuffer cmd, Device& device, DescriptorHeaps& heaps, const Swapchain& swap, const FrameContext& frame);
using HostReleaseFn = void (*)(void* user, Device& device);

// Приложение задаёт размер окна и функцию сборки холста. Кадр, swapchain и SDL живут здесь.
struct ViewDesc {
    const char* name = nullptr;
    WindowConfig window{};
    FrameDesc frame{};
    UiBuilder builder = nullptr;
    float chromeTitle = 40.0f;
    float chromeBorder = 6.0f;
    HostOpsFn onOps = nullptr;
    HostCloseFn onClose = nullptr;
    HostDropFn onDrop = nullptr;
    HostSceneTickFn sceneTick = nullptr;
    HostSceneRecordFn sceneRecord = nullptr;
    void* user = nullptr;
};

struct HostView {
    bool on = false;
    char name[24]{};
    FrameDesc frame{};
    float chromeTitle = 40.0f;
    float chromeBorder = 6.0f;
    Window window{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    Swapchain swap{};
    DescriptorHeaps heaps{};
    Canvas canvas{};
    HostOpsFn onOps = nullptr;
    HostCloseFn onClose = nullptr;
    HostDropFn onDrop = nullptr;
    HostSceneTickFn sceneTick = nullptr;
    HostSceneRecordFn sceneRecord = nullptr;
    void* user = nullptr;
};

struct Host {
    Device device;
    FrameArena arena;
    HostView view[kHostSlots]{};
    int primary = -1;
    bool ready = false;
    uint64_t clockTick = 0;
    double seconds = 0.0;
    float frameDt = 0.0f;
    uint64_t paceNext = 0;
    HostReleaseFn releaseGpu = nullptr;
    void* releaseUser = nullptr;
};

[[nodiscard]] bool hostInit(Host& host);
void hostShutdown(Host& host);
// Bytes of device memory the host's GPU allocator currently holds.
[[nodiscard]] uint64_t hostGpuMemoryUsed(const Host& host);
// То же имя поднимает уже открытое окно. Новое окно — первый свободный слот.
[[nodiscard]] int hostOpen(Host& host, const ViewDesc& desc);
void hostClose(Host& host, int slot);
[[nodiscard]] bool hostSetFrame(Host& host, int slot, const FrameDesc& frame);
[[nodiscard]] FrameDesc hostFrame(const Host& host, int slot);
[[nodiscard]] bool hostTick(Host& host);
[[nodiscard]] bool hostQuit(const Host& host);
[[nodiscard]] Canvas* hostCanvas(Host& host, int slot);
// Opens the colour picker as its own OS window when the book asks for it (present == 2) and none is open yet.
// Closing the window resets the book. Returns true when a window was opened.
bool hostSyncColorWindow(Host& host, ColorBook* colors);

} // namespace burnhope
