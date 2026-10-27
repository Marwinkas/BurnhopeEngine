#include "gui/layout/GuiScissor.hpp"

namespace burnhope::gui {
namespace {

void clipNode(flecs::entity e, const UiClip& inherited) {
    UiClip childInherited = inherited;
    if (const UiBox* box = e.try_get<UiBox>()) {
        const UiClip own = guiClipIntersect(inherited, UiClip{.x = box->x, .y = box->y, .w = box->w, .h = box->h});
        if (UiClip* clip = e.try_get_mut<UiClip>()) {
            *clip = own;
        } else {
            e.set<UiClip>(own);
        }
        if (e.has<UiClipChildren>()) {
            childInherited = own;
        }
    }
    e.children([&](flecs::entity child) { clipNode(child, childInherited); });
}

} // namespace

void guiScissorRun(flecs::world& w, float screenW, float screenH) {
    const UiClip screen{.x = 0.0f, .y = 0.0f, .w = screenW, .h = screenH};
    w.defer_begin();
    w.each(w.component<UiRoot>().id(), [&](flecs::entity root) { clipNode(root, screen); });
    w.defer_end();
}

} // namespace burnhope::gui
