#pragma once

#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kFramesInFlight = 2;

// 0 ждёт кадр (FIFO), 1 mailbox, 2 без ожидания, 3 FIFO relaxed. Нет режима — остаётся FIFO.
enum class Present : uint8_t { Vsync = 0, Mailbox = 1, Immediate = 2, Relaxed = 3 };

struct FrameDesc {
    Present present = Present::Mailbox;
    bool transparent = false;
    bool clipped = true;
    int hz = 0;
};

struct Swapchain {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR handle = VK_NULL_HANDLE;
    // Свой пул на окно (план, Фаза 1.5 — задел под job system): раньше cmd[] всех окон шли из
    // одного d.commandPool, и запись из разных потоков была бы запрещена спецификацией (пул не
    // потокобезопасен сам по себе). Это не добавляет потоки сейчас — hostTick остаётся
    // последовательным, — но структурно убирает единственное, что мешало бы параллельной записи
    // позже. Следующий шаг — мьютекс вокруг vkQueueSubmit2 (общая очередь) и сами потоки; не сделано.
    // Один пул на кадр в полёте. Флаг создания 0: сброс только vkResetCommandPool,
    // после того как fence этого кадра уже подождали. Соседний кадр в другом пуле.
    VkCommandPool pool[kFramesInFlight]{};
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent{};
    uint32_t imageCount = 0;
    VkImage images[8]{};
    VkImageView views[8]{};
    // 1 after this image has been presented. The next acquire is PRESENT_SRC, not UNDEFINED.
    uint8_t presented[8]{};
    // 1 after swapchainBegin moved this image to COLOR_ATTACHMENT this frame.
    uint8_t inColor[8]{};

    VkCommandBuffer cmd[kFramesInFlight]{};
    // Холст. Сцена его не пишет. Сабмит идёт следом за буфером сцены.
    VkCommandBuffer uiCmd[kFramesInFlight]{};
    VkSemaphore acquireSem[kFramesInFlight]{};
    // Сцена сигналит, холст ждёт. Иначе тонмап с LOAD_CLEAR может лечь поверх уже нарисованного UI.
    VkSemaphore uiSem[kFramesInFlight]{};
    VkSemaphore renderSem[8]{};
    VkFence flightFence[kFramesInFlight]{};
    VkSemaphore orderSem = VK_NULL_HANDLE;
    uint64_t orderValue = 0;
    uint64_t presentId = 0;
    // 1 пока кадр в полёте и этот тик его не дождался. Resize не ждёт вечно.
    uint8_t gpuBusy = 0;
    uint64_t clickTick = 0;
    float photonMs = 0;
    uint32_t frame = 0;
    FrameDesc asked{};
};

struct FrameContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBuffer uiCmd = VK_NULL_HANDLE;
    uint32_t imageIndex = 0;
    uint32_t flight = 0;
};

[[nodiscard]] bool swapchainCreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h);
[[nodiscard]] bool swapchainCreate(Swapchain& sc, Device& d, VkSurfaceKHR surface, uint32_t w, uint32_t h, const FrameDesc& frame = {}, VkSwapchainKHR retired = VK_NULL_HANDLE);
void swapchainDestroy(Swapchain& sc, Device& d);
[[nodiscard]] bool swapchainRecreate(Swapchain& sc, Device& d, uint32_t w, uint32_t h);

[[nodiscard]] bool swapchainBegin(Swapchain& sc, Device& d, FrameContext& fc);
// Открывает буфер холста этого кадра. Картинку свопчейна не трогает: переход уже в буфере сцены.
[[nodiscard]] bool swapchainBeginUi(Swapchain& sc, FrameContext& fc);
void swapchainToPresent(Swapchain& sc, const FrameContext& fc);
// pace: true блокирует до реального появления кадра на экране (present_wait, фотон-задержка).
// Нужно только для того окна, где важна задержка клика (главное/активное). Второе и третье окно
// с pace=true последовательно в одном потоке (core/host/Host.cpp) ждут vsync друг за другом —
// это и есть источник "второе окно подлагивает", не отдельный баг окна.
[[nodiscard]] bool swapchainSubmitPresent(Swapchain& sc, Device& d, FrameContext& fc, bool pace = true);

} // namespace burnhope
