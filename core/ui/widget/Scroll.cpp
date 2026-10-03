#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

void scrollBy(UiState& s, uint16_t id, float wheel) {
    auto* scroll = s.ent[id].try_get_mut<UiScroll>();
    if (scroll == nullptr || scroll->inner == kUiNone) {
        return;
    }
    const UiVirtual* virt = s.ent[id].try_get<UiVirtual>();
    const float viewH = s.box[id].h;
    const float contentH = virt != nullptr ? virt->count * virt->rowH : s.box[scroll->inner].h;
    float maxOff = contentH - viewH;
    if (maxOff < 0.0f) {
        maxOff = 0.0f;
    }
    scroll->offset -= wheel * 28.0f;
    if (scroll->offset < 0.0f) {
        scroll->offset = 0.0f;
    }
    if (scroll->offset > maxOff) {
        scroll->offset = maxOff;
    }
    if (virt != nullptr) {
        uiVirtualRefresh(s, id);
        return;
    }
    auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>();
    flex->marginT = -scroll->offset;
    uiMarkLayout(s, id);
    uiTrace(s, "scroll", id);
}

bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb) {
    if (id >= s.count) {
        return false;
    }
    const UiScroll* scroll = s.ent[id].try_get<UiScroll>();
    if (scroll == nullptr || scroll->inner == kUiNone || scroll->inner >= s.count) {
        return false;
    }
    const float viewH = s.box[id].h;
    const UiVirtual* virtThumb = s.ent[id].try_get<UiVirtual>();
    const float contentH = virtThumb != nullptr ? virtThumb->count * virtThumb->rowH : s.box[scroll->inner].h;
    if (viewH < 8.0f || contentH <= viewH + 1.0f) {
        return false;
    }
    float thumbH = viewH * viewH / contentH;
    if (thumbH < 18.0f) {
        thumbH = 18.0f;
    }
    if (thumbH > viewH) {
        thumbH = viewH;
    }
    const float maxOff = contentH - viewH;
    const float travel = viewH - thumbH;
    float t = maxOff > 0.0f ? scroll->offset / maxOff : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    thumb.x = s.box[id].x + s.box[id].w - 8.0f;
    thumb.y = s.box[id].y + t * travel;
    thumb.w = 6.0f;
    thumb.h = thumbH;
    return true;
}

void dragScroll(UiState& s, uint16_t id, float y) {
    auto* scroll = id < s.count ? s.ent[id].try_get_mut<UiScroll>() : nullptr;
    if (scroll == nullptr || scroll->inner == kUiNone || scroll->inner >= s.count) {
        return;
    }
    const float viewH = s.box[id].h;
    const UiVirtual* virtDrag = s.ent[id].try_get<UiVirtual>();
    const float contentH = virtDrag != nullptr ? virtDrag->count * virtDrag->rowH : s.box[scroll->inner].h;
    const float maxOff = contentH - viewH;
    if (maxOff <= 0.0f || viewH < 8.0f) {
        return;
    }
    float thumbH = viewH * viewH / contentH;
    if (thumbH < 18.0f) {
        thumbH = 18.0f;
    }
    if (thumbH > viewH) {
        thumbH = viewH;
    }
    const float travel = viewH - thumbH;
    float t = travel > 1.0f ? (y - s.box[id].y - thumbH * 0.5f) / travel : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    scroll->offset = t * maxOff;
    if (s.ent[id].try_get<UiVirtual>() != nullptr) {
        uiVirtualRefresh(s, id);
        return;
    }
    if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
        flex->marginT = -scroll->offset;
    }
    uiMarkLayout(s, id);
    s.visualDirty = true;
}

uint16_t scrollOwner(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        if (s.ent[p].try_get<UiScroll>() != nullptr) {
            return p;
        }
    }
    return kUiNone;
}

} // namespace burnhope
