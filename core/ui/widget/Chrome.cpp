#include "ui/widget/Detail.hpp"

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
    const uint16_t minB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Button, 0.22f, 0.24f, 0.28f, 6.0f, -2));
    const uint16_t maxB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Button, 0.22f, 0.24f, 0.28f, 6.0f, -3));
    const uint16_t closeB = spawn(s, cluster, px(28.0f, 28.0f), paint(UiRole::Close, 0.55f, 0.28f, 0.28f, 6.0f));
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

} // namespace burnhope
