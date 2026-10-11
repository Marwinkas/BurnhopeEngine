#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

float scrollContentH(const UiState& s, uint16_t inner) {
    float sum = 0.0f;
    int n = 0;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.parentOf[id] != inner) {
            continue;
        }
        if (s.yoga[id] != nullptr && YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone) {
            continue;
        }
        float h = 0.0f;
        if (const UiFlex* flex = s.ent[id].try_get<UiFlex>()) {
            if (flex->heightMode == static_cast<uint8_t>(UiSize::Px) && flex->height > 1.0f) {
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
        if (host->heightMode == static_cast<uint8_t>(UiSize::Px) && host->height > sum) {
            sum = host->height;
        }
    }
    if (n > 1) {
        sum += gap * static_cast<float>(n - 1);
    }
    sum += pad * 2.0f;
    return sum > 0.0f ? sum : 0.0f;
}

void scrollBy(UiState& s, uint16_t id, float wheel) {
    auto* scroll = s.ent[id].try_get_mut<UiScroll>();
    if (scroll == nullptr || scroll->inner == kUiNone) {
        return;
    }
    const float viewH = s.box[id].h;
    const UiVirtualWindow list = s.ent[id].try_get<UiVirtual>() != nullptr
        ? uiVirtualCalc(0.0f, viewH, s.ent[id].try_get<UiVirtual>()->rowH, s.ent[id].try_get<UiVirtual>()->count, 1)
        : UiVirtualWindow{};
    const UiTreeView* tree = s.ent[id].try_get<UiTreeView>();
    const UiVirtualGrid* grid = s.ent[id].try_get<UiVirtualGrid>();
    float contentH = scrollContentH(s, scroll->inner);
    if (s.ent[id].try_get<UiVirtual>() != nullptr) {
        contentH = list.totalHeight;
    } else if (tree != nullptr) {
        contentH = uiVirtualCalc(0.0f, viewH, tree->rowH, tree->count, 1).totalHeight;
    } else if (grid != nullptr) {
        contentH = uiVirtualCalc(0.0f, viewH, grid->itemH, grid->count, grid->cols < 1 ? 1 : grid->cols).totalHeight;
    }
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
    if (s.ent[id].try_get<UiVirtual>() != nullptr) {
        uiVirtualRefresh(s, id);
        return;
    }
    if (s.ent[id].try_get<UiTreeView>() != nullptr) {
        uiTreeViewRefresh(s, id);
    } else if (s.ent[id].try_get<UiVirtualGrid>() != nullptr) {
        uiVirtualGridRefresh(s, id);
    }
    auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>();
    const float next = -scroll->offset;
    if (flex->position != 0 || std::fabs(flex->posY) > 0.5f || std::fabs(flex->marginT - next) > 0.5f) {
        flex->position = 0;
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
    float contentH = scrollContentH(s, scroll->inner);
    if (const UiVirtual* virtThumb = s.ent[id].try_get<UiVirtual>()) {
        contentH = uiVirtualCalc(0.0f, viewH, virtThumb->rowH, virtThumb->count, 1).totalHeight;
    } else if (const UiTreeView* treeThumb = s.ent[id].try_get<UiTreeView>()) {
        contentH = uiVirtualCalc(0.0f, viewH, treeThumb->rowH, treeThumb->count, 1).totalHeight;
    } else if (const UiVirtualGrid* gridThumb = s.ent[id].try_get<UiVirtualGrid>()) {
        const uint8_t cols = gridThumb->cols < 1 ? 1 : gridThumb->cols;
        contentH = uiVirtualCalc(0.0f, viewH, gridThumb->itemH, gridThumb->count, cols).totalHeight;
    }
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
    if (thumb.x < s.box[id].x) {
        thumb.x = s.box[id].x;
    }
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
    float contentH = scrollContentH(s, scroll->inner);
    if (const UiVirtual* virtDrag = s.ent[id].try_get<UiVirtual>()) {
        contentH = uiVirtualCalc(0.0f, viewH, virtDrag->rowH, virtDrag->count, 1).totalHeight;
    } else if (const UiTreeView* treeDrag = s.ent[id].try_get<UiTreeView>()) {
        contentH = uiVirtualCalc(0.0f, viewH, treeDrag->rowH, treeDrag->count, 1).totalHeight;
    } else if (const UiVirtualGrid* gridDrag = s.ent[id].try_get<UiVirtualGrid>()) {
        const uint8_t cols = gridDrag->cols < 1 ? 1 : gridDrag->cols;
        contentH = uiVirtualCalc(0.0f, viewH, gridDrag->itemH, gridDrag->count, cols).totalHeight;
    }
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
    if (s.ent[id].try_get<UiTreeView>() != nullptr) {
        uiTreeViewRefresh(s, id);
    } else if (s.ent[id].try_get<UiVirtualGrid>() != nullptr) {
        uiVirtualGridRefresh(s, id);
    }
    if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
        flex->position = 0;
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

} // namespace burnhope
