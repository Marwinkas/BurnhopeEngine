#include "app/Engine.hpp"
#include "app/Shell.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

void onColorClose(Host&, int, void* user) {
    auto* colors = static_cast<ColorBook*>(user);
    colors->present = 0;
    ++colors->gen;
}

void openNamed(Host& host, const ViewDesc& desc) {
    (void)hostOpen(host, desc);
}

void onShellOps(Host& host, int, const WindowOps& ops, void* user) {
    auto* colors = static_cast<ColorBook*>(user);
    if (ops.openTool) {
        ViewDesc desc{};
        desc.name = "tool";
        desc.window.title = "Burnhope";
        desc.window.width = 560;
        desc.window.height = 420;
        desc.window.minWidth = 360;
        desc.window.minHeight = 240;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = toolBuild;
        desc.onOps = onShellOps;
        desc.user = colors;
        openNamed(host, desc);
    }
    if (ops.openColor) {
        ViewDesc desc{};
        desc.name = "color";
        desc.window.title = "Color";
        desc.window.width = 440;
        desc.window.height = 720;
        desc.window.minWidth = 360;
        desc.window.minHeight = 480;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = colorBuild;
        desc.book = colors;
        desc.onOps = onShellOps;
        desc.onClose = onColorClose;
        desc.user = colors;
        openNamed(host, desc);
    }
}

} // namespace

bool engineInit(Engine& e, int argc, char** argv) {
    (void)argc;
    (void)argv;
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Burnhope — Vulkan 1.4 core + Yoga/Flecs canvas.");
    if (!hostInit(e.host)) {
        return false;
    }
    ViewDesc main{};
    main.name = "main";
    main.window.title = "Burnhope";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = false;
    main.window.resizable = true;
    main.frame.present = Present::Mailbox;
    main.builder = shellBuild;
    main.book = &e.colors;
    main.onOps = onShellOps;
    main.user = &e.colors;
    if (hostOpen(e.host, main) < 0) {
        return false;
    }
    return true;
}

void engineShutdown(Engine& e) {
    hostShutdown(e.host);
}

bool engineTick(Engine& e) {
    return hostTick(e.host);
}

} // namespace burnhope
