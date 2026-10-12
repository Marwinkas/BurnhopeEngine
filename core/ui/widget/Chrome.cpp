#include "ui/widget/Chrome.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/Domain.hpp"

namespace burnhope {
namespace {

void onMin(void* user, uint16_t, int16_t) {
    static_cast<UiState*>(user)->opMinimize = true;
}

void onMax(void* user, uint16_t, int16_t) {
    static_cast<UiState*>(user)->opMaximize = true;
}

} // namespace

uint16_t uiWindowChrome(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Px);
    row.width = 100.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = 1;
    row.justify = 1;
    row.gap = 4.0f;
    row.marginR = 8.0f;
    const uint16_t cluster = spawn(s, parent, row, paint(UiRole::Hidden, 0, 0, 0));
    const uint16_t minB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiRole::Button, UiToken::Chrome, s.look.radiusSm, -2));
    const uint16_t maxB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiRole::Button, UiToken::Chrome, s.look.radiusSm, -3));
    const uint16_t closeB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiRole::Close, UiToken::Close, s.look.radiusSm));
    if (auto* icon = s.ent[minB].try_get_mut<UiPaint>()) {
        icon->icon = 1;
    }
    if (auto* icon = s.ent[maxB].try_get_mut<UiPaint>()) {
        icon->icon = 2;
    }
    if (auto* icon = s.ent[closeB].try_get_mut<UiPaint>()) {
        icon->icon = 3;
    }
    uiBind(s, minB, onMin, &s);
    uiBind(s, maxB, onMax, &s);
    return cluster;
}

uint16_t loadChromeBin(UiState& s, uint16_t parent, const BinNode&, const char* name, void*) {
    const uint16_t id = uiWindowChrome(s, parent);
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    return id;
}

void uiInstallChromeBin(UiState& s) {
    uiRegisterCustomBinNode(s, 10, loadChromeBin, nullptr);
}

void uiMarkTab(UiState& s, uint16_t strip, uint16_t tab) {
    const UiLook& look = s.look;
    for (uint16_t id = 1; id < s.count; ++id) {
        if (s.parentOf[id] != strip) {
            continue;
        }
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Button)) {
            continue;
        }
        const float* rgb = id == tab ? look.accent[0] : look.button;
        if (auto* live = s.ent[id].try_get_mut<UiPaint>()) {
            live->r = rgb[0];
            live->g = rgb[1];
            live->b = rgb[2];
            live->a = 1.0f;
            live->token = static_cast<uint8_t>(UiToken::Custom);
            s.visualDirty = true;
        }
    }
}

} // namespace burnhope
