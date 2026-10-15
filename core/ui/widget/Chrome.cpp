#include "ui/widget/Chrome.hpp"
#include "ui/widget/Detail.hpp"

namespace burnhope {
namespace {

void onMin(void* user, uint16_t) {
    static_cast<UiState*>(user)->opMinimize = true;
}

void onMax(void* user, uint16_t) {
    static_cast<UiState*>(user)->opMaximize = true;
}

} // namespace

uint16_t uiWindowChrome(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.widthMode = UiSize::Px;
    row.width = 100.0f;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    row.justify = UiJustify::Center;
    row.gap = 4.0f;
    row.marginR = 8.0f;
    const uint16_t cluster = spawn(s, parent, row, paintClear());
    const uint16_t minB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t maxB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t closeB = spawn(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Close, s.look.radiusSm));
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
    for (uint16_t id = 1; id < s.count; ++id) {
        if (s.parentOf[id] != strip) {
            continue;
        }
        if (!s.ent[id].has<UiFocusable>()) {
            continue;
        }
        if (auto* live = s.ent[id].try_get_mut<UiPaint>()) {
            live->token = id == tab ? UiToken::Accent : UiToken::Button;
            s.visualDirty = true;
        }
    }
}

} // namespace burnhope
