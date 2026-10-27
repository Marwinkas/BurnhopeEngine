#include "ui/widget/Popover.hpp"

#include "ui/widget/Menu.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

namespace burnhope {
void uiSyncPopover(UiState& s, uint16_t panel, uint16_t anchor, float width, float height, uint8_t& want) {
    if (panel >= s.count) {
        return;
    }
    if (want == 2 && (!uiMenuShown(s) || uiMarked<UiMenuOpen>(s) != panel)) {
        want = 0;
    }
    if ((want == 1 || want == 2) && anchor < s.count) {
        if (auto* flex = s.ent[panel].try_get_mut<UiFlex>()) {
            const UiBox& box = s.box[anchor];
            flex->position = UiPosition::Absolute;
            const float edge = uiStyle(s, "popover", UiStyleKey::ScreenGap);
            const float gap = uiStyle(s, "popover", UiStyleKey::AnchorGap);
            float x = box.x + box.w - width;
            if (x + width > s.laidW - edge) {
                x = s.laidW - width - edge;
            }
            if (x < edge) {
                x = edge;
            }
            flex->posX = x;
            float y = box.y + box.h + gap;
            // The bar is the anchor's toolbar row; a grandparent taller than a toolbar is a whole panel, not a bar to clear.
            uint16_t bar = s.parentOf[anchor];
            if (bar < s.count && s.parentOf[bar] < s.count && s.box[s.parentOf[bar]].h <= uiStyle(s, "popover", UiStyleKey::BarMaxH)) {
                bar = s.parentOf[bar];
            }
            if (bar < s.count && s.box[bar].h > 1.0f) {
                const float under = s.box[bar].y + s.box[bar].h + gap;
                if (under > y) {
                    y = under;
                }
            }
            flex->posY = y;
        }
        if (want == 1) {
            uiReveal(s, panel, height, true);
            uiMarkOnly<UiMenuOpen>(s, panel);
            uiMenuSetShown(s, true);
            uiNotifyLayoutDirty(s);
            want = 2;
        }
    } else if (want == 0) {
        uiReveal(s, panel, height, false);
        if (uiMarked<UiMenuOpen>(s) == panel) {
            uiMarkOnly<UiMenuOpen>(s, kUiNone);
            uiMenuSetShown(s, false);
        }
    }
}

} // namespace burnhope
