#include "BindlessSetup.hpp"
#include "Engine.hpp"

namespace burnhope {

void Engine::rebuildGBufferDescriptorSets() {
  // rebuildBatches() calls refreshBindlessSlots() and rewrites ObjectData heap indices.
  if (geometryRenderSystem) {
    rebuildBatches(world, *geometryRenderSystem);
  } else {
    refreshBindlessSlots(*this);
  }
}

} // namespace burnhope
