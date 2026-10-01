#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h) {
    if (slot >= kPhotoSlots) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = w;
    flex.height = h;
    flex.shrink = 0.0f;
    const uint16_t id = spawn(s, parent, flex, paint(UiRole::Panel, 1.0f, 1.0f, 1.0f, 4.0f, -90));
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

void tickPhoto(UiState& s) {
    float dt = s.time - s.prevTime;
    if (s.prevTime <= 0.0f || dt < 0.0f || dt > 0.5f) {
        dt = 0.0f;
    }
    s.prevTime = s.time;
    if (s.photoView == kUiNone || s.photoView >= s.count) {
        return;
    }
    auto* image = s.ent[s.photoView].try_get_mut<UiImage>();
    if (image == nullptr || image->frames < 2) {
        return;
    }
    image->acc += dt * 1000.0f;
    uint16_t delay = image->delayMs[image->frame];
    if (delay < 20) {
        delay = 100;
    }
    if (image->acc >= static_cast<float>(delay)) {
        image->acc = 0.0f;
        image->frame = static_cast<uint8_t>((image->frame + 1u) % image->frames);
        s.visualDirty = true;
    }
}

} // namespace burnhope
