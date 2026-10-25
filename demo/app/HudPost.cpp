#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/SceneBlockFill.hpp"

#include "io/File.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Menu.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

void onImportFlag(void* user, uint16_t, uint8_t, uint16_t button, const char* label) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr) {
        return;
    }
    sceneBlockApply(*hud);
    if (hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& state = *hud->tool.canvas->state;
    uiMenuCaption(state, button, label);
    propApplyShow(state, hud->bind);
}

void onImportSwatch(void* user, UiState& state, uint16_t id) {
    uiBind(state, id, onHudSwatch, user);
}

bool hudImportLayout(UiState& state, uint16_t parent, const char* asset, const char* key, HudContext& hud, const PropSlot* slots, uint16_t slotCount, const PropCommand* commands, uint16_t commandCount, const UiPaint& hidden) {
    char path[512]{};
    if (asset == nullptr || !platformResolveAsset(asset, path, sizeof(path))) {
        spdlog::error("inspector layout was not found");
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        spdlog::error("inspector layout did not map");
        return false;
    }
    const std::string_view text{reinterpret_cast<const char*>(file.data), file.size};
    const bool ok = propImport(state, parent, text, key, hud.bind, slots, slotCount, commands, commandCount, hidden, onImportFlag, onImportSwatch, &hud);
    fileUnmap(&file);
    if (!ok) {
        spdlog::error("inspector layout {} did not import", key != nullptr ? key : "");
    }
    return ok;
}

void hudBuildPost(Canvas& canvas, HudContext* hud, uint16_t details, UiPaint hidden) {
    if (canvas.state == nullptr || hud == nullptr) {
        return;
    }
    const PropSlot slots[] = {
        {"diBody", &hud->diBody},
        {"indBody", &hud->indBody},
        {"reflBody", &hud->reflBody},
        {"rtBody", &hud->rtBody},
        {"aoBody", &hud->aoBody},
        {"sscsBody", &hud->sscsBody},
        {"bloomBody", &hud->bloomBody},
        {"fsrBody", &hud->fsrBody},
        {"fogBody", &hud->fogBody},
        {"fogSwatch", &hud->fogSwatch},
        {"sunSwatch", &hud->sunSwatch},
        {"wxBtn", &hud->wxBtn},
        {"menuTone", &hud->menuBtn[kMenuTone]},
        {"menuAa", &hud->menuBtn[kMenuAa]},
        {"menuFsr", &hud->menuBtn[kMenuFsr]},
        {"menuGi", &hud->menuBtn[kMenuGi]},
        {"menuDirect", &hud->menuBtn[kMenuDirect]},
        {"menuReflect", &hud->menuBtn[kMenuReflect]},
        {"menuAo", &hud->menuBtn[kMenuAo]},
        {"menuSun", &hud->menuBtn[kMenuSun]},
    };
    const PropCommand commands[] = {
        {"weather.clear", wxClear, hud},
        {"weather.overcast", wxOvercast, hud},
        {"weather.rain", wxRain, hud},
        {"weather.storm", wxStorm, hud},
    };
    (void)hudImportLayout(*canvas.state, details, "assets/ui/inspector.json", "post", *hud, slots, static_cast<uint16_t>(sizeof slots / sizeof slots[0]), commands, 4, hidden);
}

} // namespace burnhope
