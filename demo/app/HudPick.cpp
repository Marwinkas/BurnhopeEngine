#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/rhi/UiPhoto.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/TreeView.hpp"
#include "Str.hpp"
#include "ui/Nodes.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void applyWeatherPreset(HudContext* hud, uint32_t preset, float coverage, float density, float rain, float wind, const char* caption) {
    if (hud == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& state = *hud->tool.canvas->state;
    hud->bind.write(state, kScCloudCoverage, coverage);
    hud->bind.write(state, kScCloudDensity, density);
    hud->bind.write(state, kScRainAmount, rain);
    hud->bind.write(state, kScWindSpeed, wind);
    uiMenuCaption(state, hud->wxBtn, caption);
    if (hud->tool.scene != nullptr) {
        sceneFrameWeatherPreset(hud->tool.scene, preset);
    }
}

void wxClear(void* user, uint16_t) { applyWeatherPreset(static_cast<HudContext*>(user), 0u, 0.12f, 0.04f, 0.0f, 2.0f, "Clear"); }
void wxOvercast(void* user, uint16_t) { applyWeatherPreset(static_cast<HudContext*>(user), 1u, 0.70f, 0.10f, 0.4f, 5.0f, "Overcast"); }
void wxRain(void* user, uint16_t) { applyWeatherPreset(static_cast<HudContext*>(user), 2u, 0.82f, 0.12f, 3.0f, 8.0f, "Rain"); }
void wxStorm(void* user, uint16_t) { applyWeatherPreset(static_cast<HudContext*>(user), 3u, 0.95f, 0.18f, 4.0f, 14.0f, "Storm"); }

void onDuplicateCmd(void* user, uint16_t) {
    onDuplicate(user, 0);
}

void onStats(void* user, uint16_t) {
    auto* hit = static_cast<StatsHit*>(user);
    if (hit == nullptr || hit->canvas == nullptr || hit->canvas->state == nullptr || hit->page == kUiNone) {
        return;
    }
    hit->open = hit->open == 0 ? 1 : 0;
    uiShow(*hit->canvas->state, hit->page, hit->open != 0);
    uiAddWidth(*hit->canvas->state, hit->dock, hit->open != 0 ? 420.0f : -420.0f, 300.0f);
}

void onStatsCmd(void* user, uint16_t) {
    onStats(user, 0);
}

void cmdView(void* user, uint16_t mode) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || hud->tool.canvas == nullptr || hud->tool.canvas->state == nullptr) {
        return;
    }
    UiState& state = *hud->tool.canvas->state;
    hud->flags.viewMode = static_cast<uint8_t>(mode);
    if (mode < 20 && hud->viewRadio[mode] != kUiNone) {
        uiCheckPress(state, hud->viewRadio[mode]);
    }
    const char* title = "Lit";
    for (const ViewMode& row : kViews) {
        if (row.index == static_cast<int>(mode)) {
            title = row.name;
        }
        if (row.index >= 0 && row.index < 20) {
            uiCheckAssign(state, hud->viewRadio[row.index], row.index == static_cast<int>(mode) ? 1 : 0);
        }
    }
    if (hud->viewCap != kUiNone) {
        uiText(state, hud->viewCap, title);
        uiTextStyle(state, hud->viewCap, UiTextAlign::Left, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        uiButtonFitText(state, hud->viewBtn, title, 38.0f);
    }
    hud->viewWant = 0;
}

} // namespace burnhope
