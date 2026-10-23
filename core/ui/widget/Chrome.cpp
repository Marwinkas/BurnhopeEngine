#include "ui/Icon.hpp"
#include "ui/UiBin.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Chrome.hpp"

namespace burnhope {
namespace {

void onMin(void* user, uint16_t) {
    uiRequestMinimize(*static_cast<UiState*>(user));
}

void onMax(void* user, uint16_t) {
    uiRequestMaximize(*static_cast<UiState*>(user));
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
    const uint16_t cluster = uiPanel(s, parent, row, paintClear());
    const uint16_t minB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t maxB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t closeB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Close, s.look.radiusSm));
    uiHover(s, minB);
    uiHover(s, maxB);
    uiHover(s, closeB);
    if (auto* icon = s.ent[minB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Minimize);
    }
    if (auto* icon = s.ent[maxB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Maximize);
    }
    if (auto* icon = s.ent[closeB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Dismiss);
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
    uiBinRole(s, kBinSlotChrome, "chrome", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotChrome, loadChromeBin, nullptr);
}

} // namespace burnhope
