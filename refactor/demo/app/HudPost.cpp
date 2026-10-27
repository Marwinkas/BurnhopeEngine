#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/SceneBlockFill.hpp"

#include "ui/widget/Button.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Menu.hpp"

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
    (void)propImportAsset(*canvas.state, details, "assets/ui/inspector.json", "post", hud->bind, slots, static_cast<uint16_t>(sizeof slots / sizeof slots[0]), commands, 4, hidden, onImportFlag, hud);
}

} // namespace burnhope
