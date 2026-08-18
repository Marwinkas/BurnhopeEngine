#pragma once
#include <string>
#include <flecs.h>

namespace burnhope {
    class BurnhopeDevice;
}

namespace burnhope::scene {

    // Loads a `.bhscene` file by mmap'ing it and casting the SoA blocks
    // directly onto the POD records declared in BHSceneFormat.hpp — no text
    // parsing. `device` may be null (headless / tooling use); when set, mesh
    // and decal records also trigger GPU resource creation, same as before.
    class BHSceneLoader {
    public:
        static bool Load(const std::string& filepath, flecs::world& world, BurnhopeDevice* device);
    };
}
