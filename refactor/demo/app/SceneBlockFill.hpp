#pragma once

#include "app/Engine.hpp"

namespace burnhope {

// One pack of the four live records into the window block. Callers do not assign scalars themselves.
void sceneBlockFill(SceneBlock& block, const HudPost& post, const SceneTweaks& tweaks, const HudFlags& flags, const HudActor& actor);

void sceneBlockPutPost(HudPost& post, uint16_t index, float value);

void sceneBlockPutActor(HudActor& actor, uint16_t index, float value);

void sceneBlockPut(HudContext& hud, uint16_t index, float value);

void sceneBlockApply(HudContext& hud);

} // namespace burnhope
