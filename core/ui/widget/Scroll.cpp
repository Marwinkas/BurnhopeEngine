#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Virtual.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace burnhope {

float scrollContentH(const UiState& s, uint16_t inner) {
    float sum = 0.0f;
    int n = 0;
    const uint32_t count = uiNodeChildCount(s, inner);
    for (uint32_t i = 0; i < count; ++i) {
        const uint16_t id = uiNodeChild(s, inner, i);
        if (id == kUiNone || !nodeShown(s, id)) {
            continue;
        }
        float h = 0.0f;
        if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
            if (flex->heightMode == UiSize::Px && flex->height > 1.0f) {
                h = flex->height;
            }
        }
        if (h < 1.0f) {
            h = s.box[id].h;
        }
        if (h > 0.0f) {
            sum += h;
            ++n;
        }
    }
    float gap = 0.0f;
    float pad = 0.0f;
    if (const UiFlex* host = s.ent[inner].try_get<UiFlex>()) {
        gap = host->gap > 0.0f ? host->gap : 0.0f;
        pad = host->pad > 0.0f ? host->pad : 0.0f;
        if (host->heightMode == UiSize::Px && host->height > sum) {
            sum = host->height;
        }
    }
    if (n > 1) {
        sum += gap * static_cast<float>(n - 1);
    }
    sum += pad * 2.0f;
    return sum > 0.0f ? sum : 0.0f;
}

float scrollSpan(const UiState& s, uint16_t id, uint16_t inner) {
    float contentH = scrollContentH(s, inner);
    if (const auto* pool = s.ent[id].try_get<UiVirtualPool>()) {
        if (pool->contentHeight != nullptr) {
            contentH = pool->contentHeight(s, id);
        }
    }
    return contentH;
}

void scrollRefresh(UiState& s, uint16_t id) {
    if (const auto* pool = s.ent[id].try_get<UiVirtualPool>()) {
        if (pool->refresh != nullptr) {
            pool->refresh(s, id);
        }
    }
}

UiScrollMetrics uiCalcScrollMetrics(float viewH, float contentH, float offset) {
    UiScrollMetrics metrics{};
    metrics.contentH = contentH;
    metrics.maxOff = contentH - viewH;
    if (metrics.maxOff < 0.0f) {
        metrics.maxOff = 0.0f;
    }
    if (viewH < 8.0f || contentH <= viewH + 1.0f) {
        metrics.thumbH = viewH;
        return metrics;
    }
    metrics.shown = 1;
    metrics.thumbH = viewH * viewH / contentH;
    if (metrics.thumbH < 18.0f) {
        metrics.thumbH = 18.0f;
    }
    if (metrics.thumbH > viewH) {
        metrics.thumbH = viewH;
    }
    metrics.travel = viewH - metrics.thumbH;
    metrics.t = metrics.maxOff > 0.0f ? offset / metrics.maxOff : 0.0f;
    if (metrics.t < 0.0f) {
        metrics.t = 0.0f;
    }
    if (metrics.t > 1.0f) {
        metrics.t = 1.0f;
    }
    return metrics;
}

void scrollClamp(UiState& s, uint16_t id) {
    auto* scroll = id < s.count ? s.ent[id].try_get_mut<UiScroll>() : nullptr;
    if (scroll == nullptr || scroll->inner == kUiNone) {
        return;
    }
    const float viewH = s.box[id].h;
    const float contentH = scrollSpan(s, id, scroll->inner);
    const float maxScroll = std::max(0.0f, contentH - viewH);
    scroll->offset = contentH <= viewH ? 0.0f : std::clamp(scroll->offset, 0.0f, maxScroll);
    if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
        flex->marginT = -scroll->offset;
    }
    scrollRefresh(s, id);
}

void scrollClampAll(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.ent[id].is_alive() && s.ent[id].try_get<UiScroll>() != nullptr) {
            scrollClamp(s, id);
        }
    }
}

void scrollBy(UiState& s, uint16_t id, float wheel) {
    auto* scroll = s.ent[id].try_get_mut<UiScroll>();
    if (scroll == nullptr || scroll->inner == kUiNone) {
        return;
    }
    const float viewH = s.box[id].h;
    const float contentH = scrollSpan(s, id, scroll->inner);
    const UiScrollMetrics metrics = uiCalcScrollMetrics(viewH, contentH, scroll->offset);
    scroll->offset -= wheel * 28.0f;
    if (scroll->offset < 0.0f) {
        scroll->offset = 0.0f;
    }
    if (scroll->offset > metrics.maxOff) {
        scroll->offset = metrics.maxOff;
    }
    uint8_t follow = 1;
    if (const UiVirtualPool* pool = s.ent[id].try_get<UiVirtualPool>()) {
        follow = pool->follow;
    }
    scrollRefresh(s, id);
    if (follow == 0) {
        uiTrace(s, "scroll", id);
        return;
    }
    auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>();
    const float next = -scroll->offset;
    if (flex != nullptr && (flex->position != UiPosition::Static || std::fabs(flex->posY) > 0.5f || std::fabs(flex->marginT - next) > 0.5f)) {
        flex->position = UiPosition::Static;
        flex->posX = 0.0f;
        flex->posY = 0.0f;
        flex->marginT = next;
        uiMarkLayout(s, id);
    }
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
    const UiScrollMetrics metrics = uiCalcScrollMetrics(viewH, scrollSpan(s, id, scroll->inner), scroll->offset);
    if (metrics.shown == 0) {
        return false;
    }
    const float travel = metrics.travel;
    const float t = metrics.t;
    thumb.x = s.box[id].x + s.box[id].w - 8.0f;
    if (thumb.x < s.box[id].x) {
        thumb.x = s.box[id].x;
    }
    thumb.y = s.box[id].y + t * travel;
    thumb.w = 6.0f;
    thumb.h = metrics.thumbH;
    return true;
}

void dragScroll(UiState& s, uint16_t id, float y) {
    auto* scroll = id < s.count ? s.ent[id].try_get_mut<UiScroll>() : nullptr;
    if (scroll == nullptr || scroll->inner == kUiNone || scroll->inner >= s.count) {
        return;
    }
    const float viewH = s.box[id].h;
    const UiScrollMetrics metrics = uiCalcScrollMetrics(viewH, scrollSpan(s, id, scroll->inner), scroll->offset);
    if (metrics.shown == 0 || metrics.maxOff <= 0.0f) {
        return;
    }
    const float travel = metrics.travel;
    const float thumbH = metrics.thumbH;
    float t = travel > 1.0f ? (y - s.box[id].y - thumbH * 0.5f) / travel : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    scroll->offset = t * metrics.maxOff;
    uint8_t follow = 1;
    if (const UiVirtualPool* pool = s.ent[id].try_get<UiVirtualPool>()) {
        follow = pool->follow;
    }
    scrollRefresh(s, id);
    if (follow == 0) {
        s.visualDirty = true;
        return;
    }
    if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
        flex->position = UiPosition::Static;
        flex->posX = 0.0f;
        flex->posY = 0.0f;
        flex->marginT = -scroll->offset;
    }
    uiMarkLayout(s, id);
    s.visualDirty = true;
}

void uiBindScroll(UiState& s, uint16_t id, uint16_t inner) {
    if (id >= s.count || inner >= s.count) {
        return;
    }
    s.ent[id].set<UiScroll>({0.0f, inner});
    s.layoutDirty = true;
}

uint16_t scrollOwner(const UiState& s, uint16_t id) {
    if (id < s.count && s.ent[id].try_get<UiScroll>() != nullptr) {
        return id;
    }
    return uiAncestorId<UiScroll>(s, id);
}

void uiScrollOverlay(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (s.ent[id].try_get<UiScroll>() == nullptr) {
            continue;
        }
        bool shown = s.box[id].w >= 1.0f && s.box[id].h >= 1.0f;
        for (uint16_t p = id; shown && p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.tight[p] != 0 || s.culled[p] != 0 || !nodeShown(s, p)) {
                shown = false;
            }
        }
        if (!shown) {
            continue;
        }
        UiBox thumb{};
        if (!scrollThumb(s, id, thumb)) {
            continue;
        }
        n = emitRect(dst, n, cap, thumb.x, thumb.y, thumb.w, thumb.h, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.95f, 2.0f, nullptr);
    }
}

bool uiScrollInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (phase == kUiInPressLate) {
        if (id >= s.count) {
            return false;
        }
        const uint16_t owner = scrollOwner(s, id);
        if (owner == kUiNone || owner >= s.count) {
            return false;
        }
        if (s.ent[owner].try_get<UiScroll>() == nullptr) {
            return false;
        }
        if (id != owner && s.ent[id].has<UiFocusable>()) {
            return false;
        }
        UiBox thumb{};
        if (scrollThumb(s, owner, thumb) && hitBox(thumb, in.pointer.x, in.pointer.y)) {
            s.capture = owner;
            dragScroll(s, owner, in.pointer.y);
            return true;
        }
        return id == owner;
    }
    if (phase == kUiInDrag) {
        if (id >= s.count) {
            return false;
        }
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (s.ent[id].try_get<UiScroll>() == nullptr) {
            return false;
        }
        dragScroll(s, id, in.pointer.y);
        return true;
    }
    if (phase == kUiInWheel) {
        const uint16_t owner = scrollOwner(s, id);
        if (owner == kUiNone) {
            return false;
        }
        scrollBy(s, owner, in.wheel);
        return true;
    }
    return false;
}

} // namespace burnhope
