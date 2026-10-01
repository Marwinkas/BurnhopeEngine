#include "app/Engine.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>

int main(int argc, char** argv) {
    burnhope::Engine engine{};
    if (!burnhope::engineInit(engine, argc, argv)) {
        burnhope::engineShutdown(engine);
        return EXIT_FAILURE;
    }

    while (!engine.window.closeRequested) {
        if (!burnhope::engineTick(engine)) {
            break;
        }
    }

    burnhope::engineShutdown(engine);
    spdlog::info("exit");
    return EXIT_SUCCESS;
}
