#include "ui/LayoutBridge.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

#include <cstdint>
#include <cstring>

#include <yoga/YGValue.h>
#include <yoga/Yoga.h>

namespace burnhope {
namespace {

YGFlexDirection flexDir(uint8_t direction) {
    if (direction == 1) {
        return YGFlexDirectionRow;
    }
    if (direction == 2) {
        return YGFlexDirectionColumnReverse;
    }
    if (direction == 3) {
        return YGFlexDirectionRowReverse;
    }
    return YGFlexDirectionColumn;
}

YGAlign flexAlign(uint8_t align) {
    if (align == 1) {
        return YGAlignCenter;
    }
    if (align == 2) {
        return YGAlignFlexStart;
    }
    if (align == 3) {
        return YGAlignFlexEnd;
    }
    if (align == 4) {
        return YGAlignBaseline;
    }
    return YGAlignStretch;
}

YGJustify flexJustify(uint8_t justify) {
    if (justify == 1) {
        return YGJustifyCenter;
    }
    if (justify == 2) {
        return YGJustifyFlexEnd;
    }
    if (justify == 3) {
        return YGJustifySpaceBetween;
    }
    if (justify == 4) {
        return YGJustifySpaceAround;
    }
    if (justify == 5) {
        return YGJustifySpaceEvenly;
    }
    return YGJustifyFlexStart;
}

YGAlign flexContent(uint8_t content) {
    if (content == 1) {
        return YGAlignCenter;
    }
    if (content == 2) {
        return YGAlignFlexStart;
    }
    if (content == 3) {
        return YGAlignFlexEnd;
    }
    if (content == 4) {
        return YGAlignSpaceBetween;
    }
    if (content == 5) {
        return YGAlignSpaceAround;
    }
    if (content == 6) {
        return YGAlignSpaceEvenly;
    }
    return YGAlignStretch;
}

YGAlign flexSelf(uint8_t self) {
    if (self == 1) {
        return YGAlignStretch;
    }
    if (self == 2) {
        return YGAlignCenter;
    }
    if (self == 3) {
        return YGAlignFlexStart;
    }
    if (self == 4) {
        return YGAlignFlexEnd;
    }
    if (self == 5) {
        return YGAlignBaseline;
    }
    return YGAlignAuto;
}

void applyFlex(YGNodeRef node, const UiFlex& f) {
    YGNodeStyleSetFlexDirection(node, flexDir(f.direction));
    YGNodeStyleSetFlexWrap(node, f.wrap == 1 ? YGWrapWrap : f.wrap == 2 ? YGWrapWrapReverse : YGWrapNoWrap);
    YGNodeStyleSetAlignItems(node, flexAlign(f.align));
    YGNodeStyleSetAlignContent(node, flexContent(f.content));
    YGNodeStyleSetAlignSelf(node, flexSelf(f.self));
    YGNodeStyleSetJustifyContent(node, flexJustify(f.justify));
    YGNodeStyleSetBoxSizing(node, f.box == 1 ? YGBoxSizingContentBox : YGBoxSizingBorderBox);
    if (f.widthMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetWidth(node, f.width);
    } else if (f.widthMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetWidthPercent(node, f.width);
    } else {
        YGNodeStyleSetWidthAuto(node);
    }
    if (f.heightMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetHeight(node, f.height);
    } else if (f.heightMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetHeightPercent(node, f.height);
    } else {
        YGNodeStyleSetHeightAuto(node);
    }
    YGNodeStyleSetFlexGrow(node, f.grow);
    YGNodeStyleSetFlexShrink(node, f.shrink);
    if (f.basisMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetFlexBasis(node, f.basis);
    } else if (f.basisMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetFlexBasisPercent(node, f.basis);
    } else {
        YGNodeStyleSetFlexBasisAuto(node);
    }
    if (f.maxWMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetMaxWidth(node, f.maxW);
    } else if (f.maxWMode == static_cast<uint8_t>(UiSize::Percent)) {
        YGNodeStyleSetMaxWidthPercent(node, f.maxW);
    } else {
        YGNodeStyleSetMaxWidth(node, YGUndefined);
    }
    if (f.maxHMode == static_cast<uint8_t>(UiSize::Px)) {
        YGNodeStyleSetMaxHeight(node, f.maxH);
    } else if (f.maxHMode == static_cast<uint8_t>(UiSize::Percent)) {
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
    const bool sides = f.padL > 0.0f || f.padR > 0.0f || f.padT > 0.0f || f.padB > 0.0f;
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
    if (f.position == 1) {
        YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    } else if (f.position == 2) {
        YGNodeStyleSetPositionType(node, YGPositionTypeRelative);
    } else {
        YGNodeStyleSetPositionType(node, YGPositionTypeStatic);
    }
    if (f.position != 0) {
        YGNodeStyleSetPosition(node, YGEdgeLeft, f.posX);
        YGNodeStyleSetPosition(node, YGEdgeTop, f.posY);
        YGNodeStyleSetPosition(node, YGEdgeRight, (f.inset & 1) != 0 ? f.posR : YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, (f.inset & 2) != 0 ? f.posB : YGUndefined);
    } else {
        YGNodeStyleSetPosition(node, YGEdgeLeft, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeTop, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeRight, YGUndefined);
        YGNodeStyleSetPosition(node, YGEdgeBottom, YGUndefined);
    }
    const YGOverflow over = f.overflow == 1 ? YGOverflowHidden : f.overflow == 2 ? YGOverflowScroll : YGOverflowVisible;
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
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    const bool clip = (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Scroll))
        || (flex != nullptr && (flex->overflow == 1 || flex->overflow == 2));
    if (clip) {
        s.ent[id].set<UiScissor>({s.box[id].x, s.box[id].y, s.box[id].x + s.box[id].w, s.box[id].y + s.box[id].h, 1});
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
    if (s.layoutFrom != kUiNone && s.layoutFrom != 0 && s.layoutFrom < s.count && s.yoga[s.layoutFrom] != nullptr
        && s.box[s.layoutFrom].w > 1.0f) {
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
        const float layW = s.box[root].w > 1.0f ? s.box[root].w : YGUndefined;
        const float layH = s.box[root].h > 1.0f ? s.box[root].h : YGUndefined;
        YGNodeCalculateLayout(s.yoga[root], layW, layH, YGDirectionLTR);
        cacheSub(cacheSub, root);
        s.layoutFrom = kUiNone;
        s.layoutDirty = false;
        s.visualDirty = true;
        return;
    }
    s.layoutFrom = kUiNone;
    auto measure = [&]() {
        for (uint16_t id = 0; id < s.count; ++id) {
            if (!s.ent[id].is_alive()) {
                continue;
            }
            const UiFlex* flex = s.ent[id].try_get<UiFlex>();
            const UiPaint* paint = s.ent[id].try_get<UiPaint>();
            if (flex == nullptr || s.yoga[id] == nullptr) {
                continue;
            }
            applyFlex(s.yoga[id], *flex);
            if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Scroll)) {
                YGNodeStyleSetOverflow(s.yoga[id], YGOverflowHidden);
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
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.culled[id] != 0 && s.yoga[id] != nullptr) {
            YGNodeStyleSetDisplay(s.yoga[id], YGDisplayFlex);
            s.culled[id] = 0;
        }
    }
    measure();
    uint16_t victims[64];
    int nv = 0;
    for (uint16_t parent = 0; parent < s.count && nv < 64; ++parent) {
        if (!s.ent[parent].is_alive()) {
            continue;
        }
        const UiFlex* flex = s.ent[parent].try_get<UiFlex>();
        if (flex == nullptr || flex->drop == 0 || s.yoga[parent] == nullptr) {
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
        int nk = 0;
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
                && ((child->minW > 1.0f && s.box[id].w + 0.5f < child->minW)
                    || (child->minH > 1.0f && s.box[id].h + 0.5f < child->minH))) {
                tight = true;
            }
        }
        if (!tight || nk < 2) {
            continue;
        }
        victims[nv++] = flex->drop == 2 ? first : last;
    }
    if (nv > 0) {
        for (int i = 0; i < nv; ++i) {
            YGNodeStyleSetDisplay(s.yoga[victims[i]], YGDisplayNone);
            s.culled[victims[i]] = 1;
        }
        measure();
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        s.tight[id] = 0;
        if (s.culled[id] != 0 || !s.ent[id].is_alive()) {
            continue;
        }
        const UiFlex* flex = s.ent[id].try_get<UiFlex>();
        if (flex == nullptr) {
            continue;
        }
        if ((flex->minW > 1.0f && s.box[id].w + 0.5f < flex->minW)
            || (flex->minH > 1.0f && s.box[id].h + 0.5f < flex->minH)) {
            s.tight[id] = 1;
        }
    }
    bool pooled = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive()) {
            continue;
        }
        if (const auto* pool = s.ent[id].try_get<UiVirtualPool>()) {
            if (pool->refresh != nullptr) {
                pool->refresh(s, id);
                pooled = true;
            }
        }
    }
    if (pooled) {
        measure();
    }
    s.laidW = w;
    s.laidH = h;
    s.layoutDirty = false;
    s.visualDirty = true;
}

void uiFlexApplyAttr(UiFlex& flex, const UiFlexAttr& attr) {
    flex.direction = attr.direction;
    flex.wrap = attr.wrap;
    flex.justify = attr.justify;
    flex.align = attr.align;
    flex.widthMode = attr.widthMode;
    flex.heightMode = attr.heightMode;
    flex.width = attr.width;
    flex.height = attr.height;
    flex.grow = attr.grow;
    flex.shrink = attr.shrink;
    flex.pad = attr.pad;
    flex.gap = attr.gap;
    flex.position = attr.position;
    flex.posX = attr.posX;
    flex.posY = attr.posY;
    flex.padT = attr.padT;
    flex.padB = attr.padB;
    flex.minW = attr.minW;
    flex.minH = attr.minH;
    flex.marginL = attr.marginL;
    flex.marginR = attr.marginR;
    flex.marginT = attr.marginT;
    flex.marginB = attr.marginB;
    flex.drop = attr.drop;
    flex.content = attr.content;
    flex.self = attr.self;
    flex.basisMode = attr.basisMode;
    flex.maxWMode = attr.maxWMode;
    flex.maxHMode = attr.maxHMode;
    flex.overflow = attr.overflow;
    flex.box = attr.box;
    flex.inset = attr.inset;
    flex.basis = attr.basis;
    flex.maxW = attr.maxW;
    flex.maxH = attr.maxH;
    flex.gapRow = attr.gapRow;
    flex.aspect = attr.aspect;
    flex.posR = attr.posR;
    flex.posB = attr.posB;
}

bool uiFlexApplyAttr(UiFlex& flex, const char* key, const char* word) {
    if (key == nullptr || word == nullptr) {
        return false;
    }
    if (std::strcmp(key, "dir") == 0) {
        if (std::strcmp(word, "row") == 0) {
            flex.direction = 1;
        } else if (std::strcmp(word, "column-reverse") == 0) {
            flex.direction = 2;
        } else if (std::strcmp(word, "row-reverse") == 0) {
            flex.direction = 3;
        } else {
            flex.direction = 0;
        }
        return true;
    }
    if (std::strcmp(key, "wrap") == 0) {
        flex.wrap = std::strcmp(word, "wrap") == 0 ? 1 : std::strcmp(word, "reverse") == 0 ? 2 : 0;
        return true;
    }
    if (std::strcmp(key, "justify") == 0) {
        uint8_t v = 0;
        if (std::strcmp(word, "center") == 0) {
            v = 1;
        } else if (std::strcmp(word, "end") == 0) {
            v = 2;
        } else if (std::strcmp(word, "between") == 0) {
            v = 3;
        } else if (std::strcmp(word, "around") == 0) {
            v = 4;
        } else if (std::strcmp(word, "evenly") == 0) {
            v = 5;
        }
        flex.justify = v;
        return true;
    }
    if (std::strcmp(key, "align") == 0 || std::strcmp(key, "content") == 0) {
        uint8_t v = 0;
        if (std::strcmp(word, "center") == 0) {
            v = 1;
        } else if (std::strcmp(word, "start") == 0) {
            v = 2;
        } else if (std::strcmp(word, "end") == 0) {
            v = 3;
        } else if (std::strcmp(word, "baseline") == 0) {
            v = 4;
        } else if (std::strcmp(word, "between") == 0) {
            v = 4;
        } else if (std::strcmp(word, "around") == 0) {
            v = 5;
        } else if (std::strcmp(word, "evenly") == 0) {
            v = 6;
        }
        if (std::strcmp(key, "align") == 0) {
            if (std::strcmp(word, "between") == 0 || std::strcmp(word, "around") == 0 || std::strcmp(word, "evenly") == 0) {
                v = 0;
            }
            flex.align = v;
        } else {
            if (std::strcmp(word, "baseline") == 0) {
                v = 0;
            }
            flex.content = v;
        }
        return true;
    }
    if (std::strcmp(key, "self") == 0) {
        uint8_t v = 0;
        if (std::strcmp(word, "stretch") == 0) {
            v = 1;
        } else if (std::strcmp(word, "center") == 0) {
            v = 2;
        } else if (std::strcmp(word, "start") == 0) {
            v = 3;
        } else if (std::strcmp(word, "end") == 0) {
            v = 4;
        } else if (std::strcmp(word, "baseline") == 0) {
            v = 5;
        }
        flex.self = v;
        return true;
    }
    if (std::strcmp(key, "overflow") == 0) {
        flex.overflow = std::strcmp(word, "hidden") == 0 ? 1 : std::strcmp(word, "scroll") == 0 ? 2 : 0;
        return true;
    }
    if (std::strcmp(key, "box") == 0) {
        flex.box = std::strcmp(word, "content") == 0 ? 1 : 0;
        return true;
    }
    if (std::strcmp(key, "position") == 0) {
        flex.position = std::strcmp(word, "absolute") == 0 ? 1 : std::strcmp(word, "relative") == 0 ? 2 : 0;
        return true;
    }
    return false;
}

bool uiFlexApplyAttr(UiFlex& flex, const char* key, float value) {
    if (key == nullptr) {
        return false;
    }
    if (std::strcmp(key, "grow") == 0) {
        flex.grow = value;
        flex.shrink = 1.0f;
        return true;
    }
    if (std::strcmp(key, "pad") == 0) {
        flex.pad = value;
        return true;
    }
    if (std::strcmp(key, "gap") == 0) {
        flex.gap = value;
        return true;
    }
    if (std::strcmp(key, "gapRow") == 0) {
        flex.gapRow = value;
        return true;
    }
    if (std::strcmp(key, "aspect") == 0) {
        flex.aspect = value;
        return true;
    }
    if (std::strcmp(key, "right") == 0) {
        flex.posR = value;
        flex.inset = static_cast<uint8_t>(flex.inset | 1);
        return true;
    }
    if (std::strcmp(key, "bottom") == 0) {
        flex.posB = value;
        flex.inset = static_cast<uint8_t>(flex.inset | 2);
        return true;
    }
    return false;
}

void uiFlexApplyAttr(UiFlex& flex, const char* key, uint8_t mode, float value) {
    if (key == nullptr) {
        return;
    }
    if (std::strcmp(key, "w") == 0) {
        flex.widthMode = mode;
        flex.width = value;
    } else if (std::strcmp(key, "h") == 0) {
        flex.heightMode = mode;
        flex.height = value;
    } else if (std::strcmp(key, "basis") == 0) {
        flex.basisMode = mode;
        flex.basis = value;
    } else if (std::strcmp(key, "maxW") == 0) {
        flex.maxWMode = mode;
        flex.maxW = value;
    } else if (std::strcmp(key, "maxH") == 0) {
        flex.maxHMode = mode;
        flex.maxH = value;
    }
}

} // namespace burnhope
