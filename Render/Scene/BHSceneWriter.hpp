#pragma once
#include <string>
#include <flecs.h>

namespace burnhope::scene {

    // Editor-time scene serializer. Not on the hot path (invoked from File >
    // Save / Ctrl+S), so std::string/std::vector usage here is acceptable —
    // it produces the flat binary `.bhscene` file consumed zero-copy by
    // BHSceneLoader.
    class BHSceneWriter {
    public:
        static bool Save(flecs::world& world, const std::string& filepath);
    };
}
