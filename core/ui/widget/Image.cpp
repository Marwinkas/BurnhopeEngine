#include "ui/Panel.hpp"
#include "ui/widget/Image.hpp"

namespace burnhope {

uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h) {
    if (slot >= kPhotoSlots) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.widthMode = UiSize::Px;
    flex.heightMode = UiSize::Px;
    flex.width = w;
    flex.height = h;
    flex.shrink = 0.0f;
    UiPaint ink = paintToken(s.look, UiToken::Content, 4.0f);
    ink.a = 0.0f;
    ink.r = 1.0f;
    ink.g = 1.0f;
    ink.b = 1.0f;
    const uint16_t id = uiPanel(s, parent, flex, ink);
    if (id == kUiNone) {
        return id;
    }
    UiImage image{};
    image.slot = slot;
    image.frames = 1;
    image.minimap = 0;
    image.viewW = 1.0f;
    image.viewH = 1.0f;
    s.ent[id].set<UiImage>(image);
    return id;
}

void uiSetIcon(UiState& s, uint16_t id, uint8_t slot) {
    auto* image = id < s.count ? s.ent[id].try_get_mut<UiImage>() : nullptr;
    if (image == nullptr) {
        return;
    }
    const bool on = slot < kPhotoSlots;
    image->slot = on ? slot : 0;
    uiShow(s, id, on);
}

void photoClamp(UiImage& image) {
    if (image.viewW < 0.08f) {
        image.viewW = 0.08f;
    }
    if (image.viewH < 0.08f) {
        image.viewH = 0.08f;
    }
    if (image.viewW > 1.0f) {
        image.viewW = 1.0f;
    }
    if (image.viewH > 1.0f) {
        image.viewH = 1.0f;
    }
    if (image.viewX < 0.0f) {
        image.viewX = 0.0f;
    }
    if (image.viewY < 0.0f) {
        image.viewY = 0.0f;
    }
    if (image.viewX + image.viewW > 1.0f) {
        image.viewX = 1.0f - image.viewW;
    }
    if (image.viewY + image.viewH > 1.0f) {
        image.viewY = 1.0f - image.viewH;
    }
}

bool photoMinimap(const UiBox& box, float x, float y, float& u, float& v) {
    constexpr float kMapW = 72.0f;
    constexpr float kMapH = 48.0f;
    constexpr float kMargin = 8.0f;
    if (box.w < 120.0f || box.h < 90.0f) {
        return false;
    }
    const float mx = box.x + box.w - kMargin - kMapW;
    const float my = box.y + box.h - kMargin - kMapH;
    if (x < mx || y < my || x >= mx + kMapW || y >= my + kMapH) {
        return false;
    }
    u = (x - mx) / kMapW;
    v = (y - my) / kMapH;
    return true;
}

void photoZoom(UiState& s, uint16_t id, float wheel, float x, float y) {
    auto* image = s.ent[id].try_get_mut<UiImage>();
    const UiBox& box = s.box[id];
    if (image == nullptr || box.w < 1.0f || box.h < 1.0f) {
        return;
    }
    const float u = (x - box.x) / box.w;
    const float v = (y - box.y) / box.h;
    const float factor = wheel > 0.0f ? 0.85f : 1.18f;
    const float cx = image->viewX + u * image->viewW;
    const float cy = image->viewY + v * image->viewH;
    image->viewW *= factor;
    image->viewH *= factor;
    image->viewX = cx - u * image->viewW;
    image->viewY = cy - v * image->viewH;
    photoClamp(*image);
    s.visualDirty = true;
}

void photoPan(UiState& s, uint16_t id, float x, float y) {
    auto* image = s.ent[id].try_get_mut<UiImage>();
    const UiBox& box = s.box[id];
    if (image == nullptr || box.w < 1.0f || box.h < 1.0f) {
        return;
    }
    image->viewX -= (x - s.grabX) / box.w * image->viewW;
    image->viewY -= (y - s.grabY) / box.h * image->viewH;
    s.grabX = x;
    s.grabY = y;
    photoClamp(*image);
    s.visualDirty = true;
}

namespace {

void photoDrag(void* user, uint16_t id, float x, float y) {
    auto& s = *static_cast<UiState*>(user);
    auto* image = id < s.count ? s.ent[id].try_get_mut<UiImage>() : nullptr;
    if (image == nullptr) {
        return;
    }
    const float dx = s.grabX - x;
    const float dy = s.grabY - y;
    const bool press = dx > -0.01f && dx < 0.01f && dy > -0.01f && dy < 0.01f;
    float u = 0.0f;
    float v = 0.0f;
    if (press && photoMinimap(s.box[id], x, y, u, v)) {
        image->viewX = u - image->viewW * 0.5f;
        image->viewY = v - image->viewH * 0.5f;
        photoClamp(*image);
        s.capture = kUiNone;
        s.visualDirty = true;
        return;
    }
    photoPan(s, id, x, y);
}

void photoWheel(void* user, uint16_t id, float wheel, float x, float y) {
    photoZoom(*static_cast<UiState*>(user), id, wheel, x, y);
}

} // namespace

void uiArmPhoto(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    s.ent[id].set<UiDrag>({photoDrag, &s});
    s.ent[id].set<UiWheel>({photoWheel, &s});
}

void uiImageBin(UiState& s) {
    uiBinRole(s, kBinSlotImage, "image", static_cast<uint8_t>(UiToken::Preview), 0);
}

} // namespace burnhope
