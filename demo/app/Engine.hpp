#pragma once

#include "host/Host.hpp"

namespace burnhope {

struct Engine {
    Host host;
    ColorBook colors{};
};

[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
