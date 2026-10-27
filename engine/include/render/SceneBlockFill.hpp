#pragma once

#include "render/SceneFrame.hpp"

namespace burnhope {

// Live inspector and post values. Sliders point here when they are created.
struct HudActor {
    float color[3]{1.0f, 1.0f, 1.0f};
    float rough = 0.5f;
    float metal = 0.0f;
    float emissive = 0.0f;
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    float pivot[3]{};
    uint8_t isStatic = 0;
};

// Modes and pass bits that are not fields of HudPost or SceneTweaks.
struct HudFlags {
    uint8_t hiz = 1;
    uint8_t cone = 1;
    uint8_t lod = 1;
    uint8_t frust = 1;
    uint8_t small = 1;
    uint8_t backf = 1;
    uint8_t scrn = 1;
    uint8_t behind = 1;
    uint8_t shadowCull = 1;
    uint8_t probes = 1;
    uint8_t cluster = 1;
    uint8_t points = 1;
    uint8_t spots = 1;
    uint8_t area = 1;
    uint8_t sky = 1;
    uint8_t clouden = 1;
    uint8_t fogen = 0;
    uint8_t bloomen = 1;
    uint8_t contacten = 0;
    uint8_t vt = 0;
    uint8_t lumScale = 0;
    uint8_t gamut = 0;
    uint8_t sun = 1;
    uint8_t sunCast = 1;
    uint8_t wxClock = 0;
    uint8_t tone = 0;
    uint8_t aa = 4;
    uint8_t fsrQ = 0;
    uint8_t giMode = 3;
    uint8_t directMode = 1;
    uint8_t reflectMode = 2;
    uint8_t aoMode = 1;
    uint8_t sunShadow = 0;
    uint8_t viewMode = 0;
    float forceLod = -1.0f;
};

// One pack of the four live records into the window block. Callers do not assign scalars themselves.
void sceneBlockFill(SceneBlock& block, const HudPost& post, const SceneTweaks& tweaks, const HudFlags& flags, const HudActor& actor);

} // namespace burnhope
