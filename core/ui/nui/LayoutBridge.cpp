#include "core/ui/LayoutBridge.hpp"
#include "core/ui/State.hpp"
#include "core/platform/Trace.hpp"

#include <cstdint>
#include <yoga/YGValue.h>
#include <yoga/Yoga.h>

namespace burnhope {
namespace {

// Потокобезопасный указатель на активный контекст верстки.
// В Фазе 2 каждый поток-воркер безопасно имеет собственный активный контекст.
thread_local UiState* tl_activeLayoutState = nullptr;

YGFlexDirection flexDir(UiFlexDir direction) noexcept {
    switch (direction) {
    case UiFlexDir::Row: return YGFlexDirectionRow;
    case UiFlexDir::ColumnReverse: return YGFlexDirectionColumnReverse;
    case UiFlexDir::RowReverse: return YGFlexDirectionRowReverse;
    case UiFlexDir::Column: default: return YGFlexDirectionColumn;
    }
}

YGAlign flexAlign(UiAlign align) noexcept {
    switch (align) {
    case UiAlign::Center: return YGAlignCenter;
    case UiAlign::Start: return YGAlignFlexStart;
    case UiAlign::End: return YGAlignFlexEnd;
    case UiAlign::Baseline: return YGAlignBaseline;
    case UiAlign::Stretch: default: return YGAlignStretch;
    }
}

YGJustify flexJustify(UiJustify justify) noexcept {
    switch (justify) {
    case UiJustify::Center: return YGJustifyCenter;
    case UiJustify::End: return YGJustifyFlexEnd;
    case UiJustify::Between: return YGJustifySpaceBetween;
    case UiJustify::Around: return YGJustifySpaceAround;
    case UiJustify::Evenly: return YGJustifySpaceEvenly;
    case UiJustify::Start: default: return YGJustifyFlexStart;
    }
}

YGAlign flexContent(UiAlignContent content) noexcept {
    switch (content) {
    case UiAlignContent::Center: return YGAlignCenter;
    case UiAlignContent::Start: return YGAlignFlexStart;
    case UiAlignContent::End: return YGAlignFlexEnd;
    case UiAlignContent::Between: return YGAlignSpaceBetween;
    case UiAlignContent::Around: return YGAlignSpaceAround;
    case UiAlignContent::Evenly: return YGAlignSpaceEvenly;
    case UiAlignContent::Stretch: default: return YGAlignStretch;
    }
}

YGAlign flexSelf(UiAlignSelf self) noexcept {
    switch (self) {
    case UiAlignSelf::Stretch: return YGAlignStretch;
    case UiAlignSelf::Center: return YGAlignCenter;
    case UiAlignSelf::Start: return YGAlignFlexStart;
    case UiAlignSelf::End: return YGAlignFlexEnd;
    case UiAlignSelf::Baseline: return YGAlignBaseline;
    case UiAlignSelf::Auto: default: return YGAlignAuto;
    }
}

// Замер произвольного содержимого (текст/картинка) через независимый интерфейс.
// Никакого разыменования rawId как указателя!
YGSize yogaMeasureAdapter(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
    UiState* s = tl_activeLayoutState;
    if (s == nullptr) {
        return { 0.0f, 0.0f };
    }

    const auto rawId = reinterpret_cast<uintptr_t>(YGNodeGetContext(node));
    const auto id = static_cast<uint16_t>(rawId);
    if (id >= s->count || !s->ent[id].is_alive()) {
        return { 0.0f, 0.0f };
    }

    if (const auto* m = s->ent[id].try_get<UiContentMeasure>()) {
        if (m->fn != nullptr) {
            float outW = 0.0f;
            float outH = 0.0f;
            const float maxW = (widthMode != YGMeasureModeUndefined) ? width : 0.0f;
            m->fn(*s, id, maxW, outW, outH, m->user);
            return { outW, outH };
        }
    }
    return { 0.0f, 0.0f };
}

void applyFlex(YGNodeRef node, const UiFlex& f) {
    YGNodeStyleSetFlexDirection(node, flexDir(f.direction));
    YGNodeStyleSetFlexWrap(node, f.wrap == UiFlexWrap::Wrap ? YGWrapWrap
                               : f.wrap == UiFlexWrap::WrapReverse ? YGWrapWrapReverse
                               : YGWrapNoWrap);
    YGNodeStyleSetAlignItems(node, flexAlign(f.align));
    YGNodeStyleSetAlignContent(node, flexContent(f.content));
    YGNodeStyleSetAlignSelf(node, flexSelf(f.self));
    YGNodeStyleSetJustifyContent(node, flexJustify(f.justify));
    YGNodeStyleSetBoxSizing(node, f.box == UiBoxSizing::ContentBox ? YGBoxSizingContentBox : YGBoxSizingBorderBox);

    if (f.widthMode == UiSize::Px) {
        YGNodeStyleSetWidth(node, f.width);
    } else if (f.widthMode == UiSize::Percent) {
        YGNodeStyleSetWidthPercent(node, f.width);
    } else {
        YGNodeStyleSetWidthAuto(node);
    }

    if (f.heightMode == UiSize::Px) {
        YGNodeStyleSetHeight(node, f.height);
    } else if (f.heightMode == UiSize::Percent) {
        YGNodeStyleSetHeightPercent(node, f.height);
    } else {
        YGNodeStyleSetHeightAuto(node);
    }

    YGNodeStyleSetFlexGrow(node, f.grow);
    YGNodeStyleSetFlexShrink(node, f.shrink);

    if (f.basisMode == UiSize::Px) {
        YGNodeStyleSetFlexBasis(node, f.basis);
    } else if (f.basisMode == UiSize::Percent) {
        YGNodeStyleSetFlexBasisPercent(node, f.basis);
    } else {
        YGNodeStyleSetFlexBasisAuto(node);
    }

    if (f.maxWMode == UiSize::Px) {
        YGNodeStyleSetMaxWidth(node, f.maxW);
    } else if (f.maxWMode == UiSize::Percent) {
        YGNodeStyleSetMaxWidthPercent(node, f.maxW);
    } else {
        YGNodeStyleSetMaxWidth(node, YGUndefined);
    }

    if (f.maxHMode == UiSize::Px) {
        YGNodeStyleSetMaxHeight(node, f.maxH);
    } else if (f.maxHMode == UiSize::Percent) {
        YGNodeStyleSetMaxHeightPercent(node, f.maxH);
    } else {
        YGNodeStyleSetMaxHeight(node, YGUndefined);
    }

    YGNodeStyleSetAspectRatio(node, f.aspect > 0.0f ? f.aspect : YGUndefined);

    if (f.minW > 0.0f) {
        YGNodeStyleSetMinWidth(node, f.minW);
    } else if (f.grow > 0.0f) {
        YGNodeStyleSetMinWidth(node, 0.0f);
    }

    if (f.minH > 0.0f) {
        YGNodeStyleSetMinHeight(node, f.minH);
    } else if (f.grow > 0.0f) {
        YGNodeStyleSetMinHeight(node, 0.0f);
    }

    const bool sides = (f.padL > 0.0f || f.padR > 0.0f || f.padT > 0.0f || f.padB > 0.0f);
    if (sides) {
        YGNodeStyleSetPadding(node, YGEdgeLeft, f.padL);
        YGNodeStyleSetPadding(node, YGEdgeRight, f.padR);
        YGNodeStyleSetPadding(node, YGEdgeTop, f.padT);
        YGNodeStyleSetPadding(node, YGEdgeBottom, f.padB);
    } else {
        YGNodeStyleSetPadding(node, YGEdgeAll, f.pad);
    }

    if (f.gapRow < 0.0f) {
        YGNodeStyleSetGap(node, YGGutterAll, f.gap);
    } else {
        YGNodeStyleSetGap(node, YGGutterColumn, f.gap);
        YGNodeStyleSetGap(node, YGGutterRow, f.gapRow);
    }

    YGNodeStyleSetMargin(node, YGEdgeLeft, f.marginL);
    YGNodeStyleSetMargin(node, YGEdgeRight, f.marginR);
    YGNodeStyleSetMargin(node, YGEdgeTop, f.marginT);
    YGNodeStyleSetMargin(node, YGEdgeBottom, f.marginB);

    if (f.position == UiPosition::Absolute) {
        YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    } else if (f.position == UiPosition::Relative) {
        YGNodeStyleSetPositionType(node, YGPositionTypeRelative);
    } else {
        YGNodeStyleSetPositionType(node, YGPositionTypeStatic);
    }

    if (f.position != UiPosition::Static) {
        YGNodeStyleSetPosition(node, YGEdgeLeft, f.posX);
        YGNodeStyleSetPosition(node, YGEdgeTop, f.posY);
        YGNodeStyleSetPosition(node, YGEdgeRight, (f.inset & UiInsetFlags::Right) ? f.posR : YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, (f.inset & UiInsetFlags::Bottom) ? f.posB : YGUndefined);
    } else {
        YGNodeStyleSetPosition(node, YGEdgeLeft, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeTop, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeRight, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, YGUndefined);
    }

    const YGOverflow over = (f.overflow == UiOverflow::Hidden || f.overflow == UiOverflow::Scroll)
                                ? YGOverflowHidden
                                : YGOverflowVisible;
    YGNodeStyleSetOverflow(node, over);
}

} // namespace

void cacheBox(UiState& s, uint16_t id) {
    const uint16_t parent = id < s.count ? s.parentOf[id] : kUiNone;
    const float left = YGNodeLayoutGetLeft(s.yoga[id]);
    const float top = YGNodeLayoutGetTop(s.yoga[id]);

    if (parent != kUiNone && parent < s.count) {
        s.box[id].x = s.box[parent].x + left;
        s.box[id].y = s.box[parent].y + top;
    } else {
        s.box[id].x = left;
        s.box[id].y = top;
    }

    s.box[id].w = YGNodeLayoutGetWidth(s.yoga[id]);
    s.box[id].h = YGNodeLayoutGetHeight(s.yoga[id]);

    s.ent[id].set<UiBounds>({s.box[id].x, s.box[id].y, s.box[id].w, s.box[id].h});

    // Ножницы ставятся строго по overflow (Hidden/Scroll). При отключении удаляются!
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    const bool clip = (flex != nullptr && (flex->overflow == UiOverflow::Hidden || flex->overflow == UiOverflow::Scroll));
    if (clip) {
        s.ent[id].set<UiScissor>({
            .minX = s.box[id].x,
            .minY = s.box[id].y,
            .maxX = s.box[id].x + s.box[id].w,
            .maxY = s.box[id].y + s.box[id].h,
            .enabled = true
        });
    } else if (s.ent[id].has<UiScissor>()) {
        s.ent[id].remove<UiScissor>();
    }
}

void uiLayout(UiState& s, float w, float h) {
    BH_ZONE;
    if (!s.layoutDirty && s.laidW == w && s.laidH == h) {
        return;
    }
    if (s.count == 0) {
        return;
    }

    // Привязываем активный контекст верстки к текущему потоку
    tl_activeLayoutState = &s;

    // Изолированная переверстка поддерева (sub-layout)
    if (s.layoutFrom != kUiNone && s.layoutFrom != 0 && s.layoutFrom < s.count && s.yoga[s.layoutFrom] != nullptr
        && s.box[s.layoutFrom].w > kUiLayoutEpsilon) {
        const uint16_t root = s.layoutFrom;
        const auto applySub = [&](auto&& self, uint16_t id) -> void {
            const UiFlex* flex = s.ent[id].try_get<UiFlex>();
            if (flex != nullptr && s.yoga[id] != nullptr) {
                applyFlex(s.yoga[id], *flex);
            }
            for (uint16_t child = 0; child < s.count; ++child) {
                if (s.parentOf[child] == id) {
                    self(self, child);
                }
            }
        };
        const auto cacheSub = [&](auto&& self, uint16_t id) -> void {
            if (s.yoga[id] != nullptr) {
                cacheBox(s, id);
            }
            for (uint16_t child = 0; child < s.count; ++child) {
                if (s.parentOf[child] == id) {
                    self(self, child);
                }
            }
        };

        applySub(applySub, root);
        const float layW = s.box[root].w > kUiLayoutEpsilon ? s.box[root].w : YGUndefined;
        const float layH = s.box[root].h > kUiLayoutEpsilon ? s.box[root].h : YGUndefined;
        YGNodeCalculateLayout(s.yoga[root], layW, layH, YGDirectionLTR);
        cacheSub(cacheSub, root);

        s.layoutFrom = kUiNone;
        s.layoutDirty = false;
        s.visualDirty = true;
        tl_activeLayoutState = nullptr;
        return;
    }

    s.layoutFrom = kUiNone;

    auto measure = [&]() {
        for (uint16_t id = 0; id < s.count; ++id) {
            if (!s.ent[id].is_alive()) {
                continue;
            }
            const UiFlex* flex = s.ent[id].try_get<UiFlex>();
            if (flex == nullptr || s.yoga[id] == nullptr) {
                continue;
            }
            applyFlex(s.yoga[id], *flex);

            if (s.ent[id].has<UiContentMeasure>()) {
                YGNodeSetMeasureFunc(s.yoga[id], yogaMeasureAdapter);
            } else if (YGNodeGetMeasureFunc(s.yoga[id]) != nullptr) {
                YGNodeSetMeasureFunc(s.yoga[id], nullptr);
            }
        }

        YGNodeStyleSetWidth(s.yoga[0], w);
        YGNodeStyleSetHeight(s.yoga[0], h);
        YGNodeCalculateLayout(s.yoga[0], w, h, YGDirectionLTR);

        const auto cacheTree = [&](auto&& self, uint16_t id) -> void {
            if (id >= s.count || s.yoga[id] == nullptr) {
                return;
            }
            cacheBox(s, id);
            const uint32_t kids = YGNodeGetChildCount(s.yoga[id]);
            for (uint32_t i = 0; i < kids; ++i) {
                const auto raw = reinterpret_cast<uintptr_t>(YGNodeGetContext(YGNodeGetChild(s.yoga[id], i)));
                if (raw < s.count) {
                    self(self, static_cast<uint16_t>(raw));
                }
            }
        };
        cacheTree(cacheTree, 0);
    };

    // Сброс скрытых ранее элементов
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.culled[id] != 0 && s.yoga[id] != nullptr) {
            YGNodeStyleSetDisplay(s.yoga[id], YGDisplayFlex);
            s.culled[id] = 0;
        }
    }

    measure();

    // FlexDrop: скрытие элементов при нехватке места
    uint16_t victims[kUiFlexDropVictimCap];
    uint16_t nv = 0;
    for (uint16_t parent = 0; parent < s.count && nv < kUiFlexDropVictimCap; ++parent) {
        if (!s.ent[parent].is_alive()) {
            continue;
        }
        const UiFlex* flex = s.ent[parent].try_get<UiFlex>();
        if (flex == nullptr || flex->drop == UiFlexDrop::None || s.yoga[parent] == nullptr) {
            continue;
        }
        if (YGNodeStyleGetDisplay(s.yoga[parent]) == YGDisplayNone) {
            continue;
        }
        const uint32_t childCount = YGNodeGetChildCount(s.yoga[parent]);
        if (childCount < 2) {
            continue;
        }

        uint16_t first = kUiNone;
        uint16_t last = kUiNone;
        uint32_t nk = 0;
        bool tight = false;

        for (uint32_t i = 0; i < childCount; ++i) {
            YGNodeRef childNode = YGNodeGetChild(s.yoga[parent], i);
            if (childNode == nullptr || YGNodeStyleGetDisplay(childNode) == YGDisplayNone) {
                continue;
            }
            const auto raw = reinterpret_cast<uintptr_t>(YGNodeGetContext(childNode));
            if (raw >= s.count) {
                continue;
            }
            const uint16_t id = static_cast<uint16_t>(raw);
            if (first == kUiNone) {
                first = id;
            }
            last = id;
            ++nk;
            const UiFlex* child = s.ent[id].try_get<UiFlex>();
            if (child != nullptr
                && ((child->minW > kUiLayoutEpsilon && s.box[id].w + kUiPixelSnap < child->minW)
                    || (child->minH > kUiLayoutEpsilon && s.box[id].h + kUiPixelSnap < child->minH))) {
                tight = true;
            }
        }

        if (!tight || nk < 2) {
            continue;
        }
        victims[nv++] = (flex->drop == UiFlexDrop::HideFirst) ? first : last;
    }

    if (nv > 0) {
        for (uint16_t i = 0; i < nv; ++i) {
            YGNodeStyleSetDisplay(s.yoga[victims[i]], YGDisplayNone);
            s.culled[victims[i]] = 1;
        }
        measure();
    }

    // Проверка сжатых нод
    for (uint16_t id = 0; id < s.count; ++id) {
        s.tight[id] = 0;
        if (s.culled[id] != 0 || !s.ent[id].is_alive()) {
            continue;
        }
        const UiFlex* flex = s.ent[id].try_get<UiFlex>();
        if (flex == nullptr) {
            continue;
        }
        if ((flex->minW > kUiLayoutEpsilon && s.box[id].w + kUiPixelSnap < flex->minW)
            || (flex->minH > kUiLayoutEpsilon && s.box[id].h + kUiPixelSnap < flex->minH)) {
            s.tight[id] = 1;
        }
    }

    // Чистая инверсия контроля: опрос универсальных компонентов обновления верстки (UiLayoutRefresh)
    bool refreshed = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive()) {
            continue;
        }
        if (const auto* r = s.ent[id].try_get<UiLayoutRefresh>()) {
            if (r->fn != nullptr) {
                r->fn(s, id, r->user);
                refreshed = true;
            }
        }
    }
    if (refreshed) {
        measure();
    }

    const bool resized = (s.laidW != w || s.laidH != h);
    s.laidW = w;
    s.laidH = h;
    s.layoutDirty = false;

    // Вызов зарегистрированного хука финализации
    if (s.layoutFinishHook.fn != nullptr) {
        s.layoutFinishHook.fn(s, resized, s.layoutFinishHook.user);
    }

    s.visualDirty = true;
    tl_activeLayoutState = nullptr;
}

void uiRegisterLayoutFinish(UiState& s, UiLayoutFinishFn fn, void* user) {
    s.layoutFinishHook = { .fn = fn, .user = user };
}

} // namespace burnhope