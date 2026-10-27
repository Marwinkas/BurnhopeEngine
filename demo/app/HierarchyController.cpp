#include "app/HierarchyController.hpp"

#include "render/SceneFrame.hpp"
#include "gpu_scene/SceneObject.hpp"

#include <cstring>

namespace burnhope {
namespace {

constexpr const char* kList = "scene.hier";

void onPick(void* user, uint32_t index, bool) {
    auto& h = *static_cast<HierarchyState*>(user);
    if (index < sceneFrameActorCount(h.scene)) {
        sceneFrameActorSelect(h.scene, static_cast<uint8_t>(index));
    }
}

} // namespace

bool hierarchyBind(HierarchyState& h, GuiHost& gui, SceneFrame* scene) {
    h.gui = &gui;
    h.scene = scene;
    h.rows.reserve(256);
    return guiHostBindList(h.gui, kList, onPick, &h);
}

void hierarchySync(HierarchyState& h) {
    const uint8_t count = sceneFrameActorCount(h.scene);
    bool changed = !h.built || h.rows.size() != count;
    h.rows.resize(count);
    for (uint8_t i = 0; i < count; ++i) {
        GuiListEntry row{};
        uint8_t kind = 0;
        sceneFrameActorName(h.scene, i, row.text, kGuiListTextCap, &kind);
        row.tone = kind == 0 ? 0 : 1;
        if (std::memcmp(&row, &h.rows[i], sizeof(row)) != 0) {
            h.rows[i] = row;
            changed = true;
        }
    }
    if (changed) {
        guiHostSetList(h.gui, kList, h.rows.data(), count);
        h.built = true;
        h.selected = 0xfe; // force the selection to be mirrored into the rebuilt list
    }
    const uint8_t selected = sceneFrameActorSelected(h.scene);
    if (selected != h.selected) {
        guiHostSetListSelected(h.gui, kList, selected < count ? selected : kGuiNoSelection);
        h.selected = selected;
    }
}

} // namespace burnhope
