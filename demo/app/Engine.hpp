#pragma once

#include "host/Host.hpp"
#include "render/SceneFrame.hpp"

namespace burnhope {

struct Engine {
    Host host;
    ColorBook colors{};
    SceneFrame* scene = nullptr;
};

[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope
