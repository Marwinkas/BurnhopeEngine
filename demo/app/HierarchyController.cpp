#include "app/HierarchyController.hpp"

#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <cstring>

namespace burnhope {
namespace {

constexpr const char* kList = "scene.hier";

void onPick(void* user, uint32_t index, bool) {
    auto& e = *static_cast<Engine*>(user);
    if (index < sceneFrameActorCount(e.scene)) {
        sceneFrameActorSelect(e.scene, static_cast<uint8_t>(index));
    }
}

} // namespace

bool hierarchyBind(Engine& e) {
    e.hierarchy.rows.reserve(256);
    return guiHostBindList(e.gui, kList, onPick, &e);
}

void hierarchySync(Engine& e) {
    HierarchyState& h = e.hierarchy;
    const uint8_t count = sceneFrameActorCount(e.scene);
    bool changed = !h.built || h.rows.size() != count;
    h.rows.resize(count);
    for (uint8_t i = 0; i < count; ++i) {
        GuiListEntry row{};
        uint8_t kind = 0;
        sceneFrameActorName(e.scene, i, row.text, kGuiListTextCap, &kind);
        row.tone = kind == 0 ? 0 : 1;
        if (std::memcmp(&row, &h.rows[i], sizeof(row)) != 0) {
            h.rows[i] = row;
            changed = true;
        }
    }
    if (changed) {
        guiHostSetList(e.gui, kList, h.rows.data(), count);
        h.built = true;
        h.selected = 0xfe; // force the selection to be mirrored into the rebuilt list
    }
    const uint8_t selected = sceneFrameActorSelected(e.scene);
    if (selected != h.selected) {
        guiHostSetListSelected(e.gui, kList, selected < count ? selected : kGuiNoSelection);
        h.selected = selected;
    }
}

} // namespace burnhope
