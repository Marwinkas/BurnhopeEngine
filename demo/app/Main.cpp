#include "app/Engine.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::strcmp(argv[i], "--import") == 0) {
            return burnhope::engineImportScene() ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }
    burnhope::Engine engine{};
    if (!burnhope::engineInit(engine, argc, argv)) {
        burnhope::engineShutdown(engine);
        return EXIT_FAILURE;
    }

    while (!burnhope::hostQuit(engine.host)) {
        if (!burnhope::engineTick(engine)) {
            break;
        }
    }

    burnhope::engineShutdown(engine);
    spdlog::info("exit");
    return EXIT_SUCCESS;
}
