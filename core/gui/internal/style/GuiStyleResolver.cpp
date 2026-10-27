#include "style/GuiStyleResolver.hpp"

#include "platform/Check.hpp"

namespace burnhope::gui {
namespace {

constexpr YGFlexDirection kDirs[] = {YGFlexDirectionColumn, YGFlexDirectionRow, YGFlexDirectionColumnReverse, YGFlexDirectionRowReverse};
constexpr YGAlign kAligns[] = {YGAlignAuto, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch, YGAlignBaseline};
constexpr YGJustify kJustifies[] = {YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                    YGJustifySpaceBetween, YGJustifySpaceAround, YGJustifySpaceEvenly};
constexpr YGWrap kWraps[] = {YGWrapNoWrap, YGWrapWrap, YGWrapWrapReverse};
constexpr YGOverflow kOverflows[] = {YGOverflowVisible, YGOverflowHidden, YGOverflowScroll};
constexpr YGPositionType kPositions[] = {YGPositionTypeRelative, YGPositionTypeAbsolute};
constexpr YGEdge kEdges[4] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};

template <size_t N>
uint8_t clampIndex(uint8_t v) {
    return v < N ? v : static_cast<uint8_t>(N - 1);
}

void applyYoga(const GuiStyle& s, YGNodeRef node) {
    const auto has = [&](GuiStyleBit bit) { return guiStyleHas(s, bit); };
    if (has(kSbWAuto)) {
        YGNodeStyleSetWidthAuto(node);
    } else if (has(kSbW)) {
        has(kSbWPercent) ? YGNodeStyleSetWidthPercent(node, s.w) : YGNodeStyleSetWidth(node, s.w);
    }
    if (has(kSbHAuto)) {
        YGNodeStyleSetHeightAuto(node);
    } else if (has(kSbH)) {
        has(kSbHPercent) ? YGNodeStyleSetHeightPercent(node, s.h) : YGNodeStyleSetHeight(node, s.h);
    }
    if (has(kSbMinW)) {
        YGNodeStyleSetMinWidth(node, s.minW);
    }
    if (has(kSbMinH)) {
        YGNodeStyleSetMinHeight(node, s.minH);
    }
    if (has(kSbMaxW)) {
        YGNodeStyleSetMaxWidth(node, s.maxW);
    }
    if (has(kSbMaxH)) {
        YGNodeStyleSetMaxHeight(node, s.maxH);
    }
    for (uint32_t i = 0; i < 4; ++i) {
        if (has(static_cast<GuiStyleBit>(kSbPadL + i))) {
            YGNodeStyleSetPadding(node, kEdges[i], s.pad[i]);
        }
        if (has(static_cast<GuiStyleBit>(kSbMarL + i))) {
            YGNodeStyleSetMargin(node, kEdges[i], s.margin[i]);
        }
        if (has(static_cast<GuiStyleBit>(kSbPosL + i))) {
            YGNodeStyleSetPosition(node, kEdges[i], s.pos[i]);
        }
    }
    if (has(kSbGap)) {
        YGNodeStyleSetGap(node, YGGutterAll, s.gap);
    }
    if (has(kSbFlexDir)) {
        YGNodeStyleSetFlexDirection(node, kDirs[clampIndex<4>(s.flexDir)]);
    }
    if (has(kSbGrow)) {
        YGNodeStyleSetFlexGrow(node, s.grow);
    }
    if (has(kSbShrink)) {
        YGNodeStyleSetFlexShrink(node, s.shrink);
    }
    if (has(kSbAlignItems)) {
        YGNodeStyleSetAlignItems(node, kAligns[clampIndex<6>(s.alignItems)]);
    }
    if (has(kSbAlignSelf)) {
        YGNodeStyleSetAlignSelf(node, kAligns[clampIndex<6>(s.alignSelf)]);
    }
    if (has(kSbJustify)) {
        YGNodeStyleSetJustifyContent(node, kJustifies[clampIndex<6>(s.justify)]);
    }
    if (has(kSbWrap)) {
        YGNodeStyleSetFlexWrap(node, kWraps[clampIndex<3>(s.wrap)]);
    }
    if (has(kSbOverflow)) {
        YGNodeStyleSetOverflow(node, kOverflows[clampIndex<3>(s.overflow)]);
    }
    if (has(kSbPosition)) {
        YGNodeStyleSetPositionType(node, kPositions[clampIndex<2>(s.position)]);
    }
}

} // namespace

bool guiStyleApply(const GuiStyleSheet& sheet, flecs::entity e, uint32_t classId) {
    const GuiStyle* style = guiStyleGet(sheet, classId);
    BH_ASSERT(style != nullptr, "guiStyleApply: unknown style class");
    if (style == nullptr || !e.is_alive()) {
        return false;
    }
    const GuiStyle& s = *style;
    e.set<UiClass>(UiClass{.id = classId});

    UiPaint& paint = e.ensure<UiPaint>();
    if (guiStyleHas(s, kSbToken)) {
        paint.token = s.token;
    }
    if (guiStyleHas(s, kSbBorderToken)) {
        paint.borderToken = s.borderToken;
    }
    if (guiStyleHas(s, kSbRadius)) {
        paint.radius = guiStyleRadius(sheet, s);
    }
    if (guiStyleHas(s, kSbBorderW)) {
        paint.borderW = s.borderW;
    }
    if (guiStyleHas(s, kSbAlpha)) {
        paint.tint[3] = s.alpha;
    }
    e.modified<UiPaint>();

    if (guiStyleHas(s, kSbTextSize) || guiStyleHas(s, kSbTextAlign)) {
        UiText& text = e.ensure<UiText>();
        if (guiStyleHas(s, kSbTextSize)) {
            text.size = s.textSize;
        }
        if (guiStyleHas(s, kSbTextAlign)) {
            text.align = s.textAlign;
        }
        e.modified<UiText>();
    }

    if (const UiFlex* flex = e.try_get<UiFlex>(); flex != nullptr && flex->node != nullptr) {
        applyYoga(s, flex->node);
    }
    return true;
}

} // namespace burnhope::gui
