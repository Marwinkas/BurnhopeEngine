#pragma once

#include <cstdint>
#include <type_traits>

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiVirtualCap = 64;   // live item entities per virtual host
constexpr uint32_t kGuiVirtualHostCap = 32; // virtual hosts processed per frame
constexpr uint32_t kGuiVirtualUnbound = 0xFFFFFFFFu;

// Per-host pool bookkeeping, owned by the pool and stored on the UiVirtual entity.
// Item i always lives in slot (i % kGuiVirtualCap), so scrolling by one row rebinds only the rows that entered.
struct GuiVirtualSlots {
    uint64_t item[kGuiVirtualCap] = {};
    float placedW = -1.0f;
    float placedH = -1.0f;
    uint32_t placedCols = 0;
    uint32_t pad0 = 0;
};

static_assert(std::is_trivially_copyable_v<GuiVirtualSlots>);

using GuiVirtualBind = void (*)(flecs::entity item, uint32_t index, void* user);

// For every entity with UiVirtual + UiScroll:
//   - writes UiScroll.contentH = ceil(count / columns) * itemH (and contentW = columns * itemW when itemW > 0)
//     and clamps offY to [0, contentH - viewH];
//   - keeps up to kGuiVirtualCap child entities (UiVirtualItem + absolute yoga box) on the visible window;
//   - calls bind(item, index, user) only when the item shown by a slot changed (new slots bind on first show).
// Returns the number of structural changes (rebinds, moves, show/hide, scroll clamps). Non-zero means layout must run again;
// running it once more afterwards returns 0 and restores contentH/viewH that layout overwrote.
// A recycled slot loses its transient tags; when interact is given and its keyboard focus points at the recycled entity,
// the focus is cleared too, so the interaction state never refers to an entity that now shows another item.
// Call after guiLayoutRun/guiScissorRun. Item entities require Module A's guiLayoutAttach and a parent with OrderedChildren.
uint32_t guiVirtualRun(flecs::world& world, GuiVirtualBind bind, void* user, GuiInteractState* interact = nullptr);

} // namespace burnhope::gui
