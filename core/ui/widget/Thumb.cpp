#include "ui/widget/Thumb.hpp"
#include "ui/State.hpp"
#include "ui/widget/Image.hpp"

namespace burnhope {

void uiThumb(UiState& s, uint16_t id, uint8_t photoSlot, UiIcon fallback) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (photoSlot < kPhotoSlots) {
        UiImage image{};
        if (const UiImage* live = s.ent[id].try_get<UiImage>()) {
            image = *live;
        }
        image.slot = photoSlot;
        image.frames = image.frames == 0 ? 1 : image.frames;
        image.viewX = 0.0f;
        image.viewY = 0.0f;
        image.viewW = 1.0f;
        image.viewH = 1.0f;
        s.ent[id].set<UiImage>(image);
        if (paint != nullptr) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
            paint->icon = static_cast<uint8_t>(UiIcon::None);
        }
        return;
    }
    if (s.ent[id].has<UiImage>()) {
        s.ent[id].remove<UiImage>();
    }
    if (paint != nullptr) {
        paint->a = 0.0f;
        paint->borderW = 0.0f;
        paint->shadow = 0.0f;
        paint->icon = static_cast<uint8_t>(fallback);
        paint->r = s.look.text[0];
        paint->g = s.look.text[1];
        paint->b = s.look.text[2];
    }
}

} // namespace burnhope
