#include "app/Shell.hpp"
#include "platform/Window.hpp"
#include "ui/widget/ColorPicker.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>

int main() {
    burnhope::Host host{};
    if (!burnhope::hostInit(host)) {
        return EXIT_FAILURE;
    }
    burnhope::ColorBook colors{};
    burnhope::ShellDemo demo{};
    demo.colors = &colors;
    demo.chipsOn = true;
    demo.toolsOn = true;
    burnhope::ViewDesc main{};
    main.name = "main";
    main.window.title = "Burnhope";
    main.window.width = 1600;
    main.window.height = 900;
    main.window.bordered = false;
    main.window.resizable = true;
    main.frame.present = burnhope::Present::Mailbox;
    main.builder = burnhope::shellBuild;
    main.onDrop = burnhope::sampleDrop;
    main.onOps = burnhope::sampleOps;
    main.user = &demo;
    if (burnhope::hostOpen(host, main) < 0) {
        burnhope::hostShutdown(host);
        return EXIT_FAILURE;
    }
    while (!burnhope::hostQuit(host)) {
        char picked[512]{};
        if (burnhope::windowTakeFile(picked, static_cast<int>(sizeof(picked))) && demo.fileDialogSlot >= 0) {
            burnhope::sampleDrop(host, demo.fileDialogSlot, picked, &demo);
            demo.fileDialogSlot = -1;
        }
        if (!burnhope::hostTick(host)) {
            break;
        }
    }
    burnhope::hostShutdown(host);
    spdlog::info("exit");
    return EXIT_SUCCESS;
}
