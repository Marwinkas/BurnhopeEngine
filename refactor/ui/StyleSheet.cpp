#include "ui/StyleSheet.hpp"

#include "io/File.hpp"
#include "platform/Check.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/State.hpp"
#include "ui/StyleInternal.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {
namespace {

// Caches the render-path classes ("ui", "minimap", "image_viewer") so emission reads plain floats.
void refreshPaintStyle(UiState& s) {
    using K = UiStyleKey;
    if (uiStyleFind(s, "ui") == nullptr || uiStyleFind(s, "minimap") == nullptr || uiStyleFind(s, "image_viewer") == nullptr) {
        return; // a partial sheet keeps the paint style it has
    }
    UiPaintStyle& p = s.paintStyle;
    p.disabledAlpha = uiStyle(s, "ui", K::DisabledAlpha);
    p.hoverLift = uiStyle(s, "ui", K::HoverLift);
    p.hoverMinAlpha = uiStyle(s, "ui", K::HoverMinAlpha);
    p.hoverFallback = uiStyle(s, "ui", K::HoverFallback);
    p.shadowDy = uiStyle(s, "ui", K::ShadowDy);
    p.shadowAlpha = uiStyle(s, "ui", K::ShadowAlpha);
    p.shadowGrow = uiStyle(s, "ui", K::ShadowGrow);
    p.iconSide = uiStyle(s, "ui", K::IconSide);
    p.iconLeft = uiStyle(s, "ui", K::Icon);
    p.iconInset = uiStyle(s, "ui", K::IconInset);
    p.iconFitMin = uiStyle(s, "ui", K::IconFitMin);
    p.iconFitPad = uiStyle(s, "ui", K::IconFitPad);
    p.iconMin = uiStyle(s, "ui", K::IconMin);
    p.iconMax = uiStyle(s, "ui", K::IconMax);
    p.toolIcon = uiStyle(s, "ui", K::ToolIcon);
    p.inkThreshold = uiStyle(s, "ui", K::InkThreshold);
    p.mapW = uiStyle(s, "minimap", K::Width);
    p.mapH = uiStyle(s, "minimap", K::Height);
    p.mapMargin = uiStyle(s, "minimap", K::Margin);
    p.mapPad = uiStyle(s, "minimap", K::Pad);
    p.mapAlpha = uiStyle(s, "minimap", K::Alpha);
    p.mapRadius = uiStyle(s, "minimap", K::Radius);
    p.mapMinW = uiStyle(s, "minimap", K::MinW);
    p.mapMinH = uiStyle(s, "minimap", K::MinH);
    p.zoomIn = uiStyle(s, "image_viewer", K::ZoomIn);
    p.zoomOut = uiStyle(s, "image_viewer", K::ZoomOut);
    p.minView = uiStyle(s, "image_viewer", K::MinView);
}

bool applySheet(UiState& s, const UiStyleSheet& sheet) {
    s.world.ensure<UiStyleSheet>() = sheet;
    refreshPaintStyle(s);
    if (uiStyleFind(s, "window_panel") != nullptr) {
        s.look.popupAlpha = uiStyle(s, "window_panel", UiStyleKey::Alpha);
        s.look.popupBorder = uiStyle(s, "window_panel", UiStyleKey::Border);
        s.look.popupRadius = uiStyle(s, "window_panel", UiStyleKey::Radius);
    }
    return true;
}

} // namespace

bool uiStyleLoad(UiState& s, std::string_view json) {
    UiStyleSheet sheet{};
    if (!style::parseSheet(json, sheet, false)) {
        return false;
    }
    return applySheet(s, sheet);
}

bool uiStyleLoadOver(UiState& s, std::string_view json) {
    UiStyleSheet sheet{};
    if (const UiStyleSheet* live = s.world.try_get<UiStyleSheet>()) {
        sheet = *live;
    }
    if (!style::parseSheet(json, sheet, true)) {
        return false;
    }
    return applySheet(s, sheet);
}


bool uiStyleLoadAsset(UiState& s, const char* asset) {
    char path[512]{};
    if (asset == nullptr || !platformResolveAsset(asset, path, sizeof(path))) {
        spdlog::error("style sheet {} was not found", asset != nullptr ? asset : "");
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        spdlog::error("style sheet {} did not map", path);
        return false;
    }
    const bool ok = uiStyleLoad(s, {reinterpret_cast<const char*>(file.data), file.size});
    fileUnmap(&file);
    if (!ok) {
        spdlog::error("style sheet {} did not parse", path);
    }
    return ok;
}

const UiStyleClass* uiStyleFind(const UiState& s, const char* name) {
    const UiStyleSheet* sheet = s.world.try_get<UiStyleSheet>();
    return sheet != nullptr && name != nullptr ? style::findIn(*sheet, name) : nullptr;
}

bool uiStyleHas(const UiStyleClass* cls, UiStyleKey key) {
    return cls != nullptr && cls->has.test(key);
}

bool uiStyleResolve(const UiState& s, const char* name, UiStyleClass& out) {
    const UiStyleClass* found = uiStyleFind(s, name);
    if (found == nullptr) {
        return false;
    }
    out = *found;
    return true;
}

UiToken uiStyleBg(const UiState& s, const char* name) {
    UiStyleClass cls;
    return uiStyleResolve(s, name, cls) ? cls.bg : UiToken::Custom;
}

float uiStyle(const UiState& s, const char* cls, UiStyleKey key) {
    const UiStyleClass* found = uiStyleFind(s, cls);
    if (uiStyleHas(found, key)) {
        return found->value[static_cast<size_t>(key)];
    }
    BH_ASSERT(false, "style class or key is not in styles.json");
    return 0.0f;
}

UiToken uiStyleTone(const UiState& s, const char* cls, UiStyleTone tone) {
    const UiStyleClass* found = uiStyleFind(s, cls);
    const UiToken token = found != nullptr ? found->tone[static_cast<size_t>(tone)] : UiToken::Custom;
    BH_ASSERT(token != UiToken::Custom, "style class or tone is not in styles.json");
    return token;
}

UiRgb uiStyleTint(const UiState& s, const char* cls, UiStyleTone tone) {
    return tokenColor(s.look, uiStyleTone(s, cls, tone));
}

const char* uiStyleToneName(UiStyleTone tone) {
    static constexpr const char* kNames[] = {
        "fillBg", "markBg", "markActiveBg", "hoverBg", "errorBg", "lineBg", "handleBg", "trackBg", "thumbBg", "todayBg", "textBg", "hintBg", "clearBg",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(UiStyleTone::Count));
    return kNames[static_cast<size_t>(tone)];
}

const char* uiStyleKeyName(UiStyleKey key) {
    static constexpr const char* kNames[] = {
        "h", "inner", "w", "maxW", "minW", "pad", "padL", "padR", "gap", "border", "radius", "icon", "alpha", "trail",
        "labelW", "fieldW", "badgeW", "lockGap", "hoverAlpha", "hoverGain", "chrome", "rowH",
        "errorBorder", "clearIcon", "labelPad",
        "iconSide", "iconInset", "iconFitMin", "shadowDy", "shadowAlpha", "shadowGrow", "disabledAlpha",
        "hoverLift", "hoverFallback", "hoverMinAlpha", "plate",
        "margin", "maxH", "minH", "caretW", "selectionAlpha", "trackH", "thumbSide", "handleSide",
        "iconFitPad", "iconMin", "iconMax", "inkThreshold", "shadow", "toolIcon", "borderDim", "clearW", "iconLeft", "marginR", "offset", "anchorGap", "screenGap", "flyoutW", "popupMinW", "minThumb", "wheelStep", "barInset", "basePane", "hitSlop", "minPane", "hotAlpha", "idleAlpha", "barMaxH", "chevron", "treeIndent", "tileGap", "gridInset", "labelH", "subH", "subScale", "listPad", "listGap", "selectedA", "headerA", "dragSlop", "offsetY", "glyphW", "rowInset", "ramp",
        "lineH", "caretInset", "imeOffset", "imeH", "imeAlpha", "selectionInset", "blinkRate", "zoomIn", "zoomOut", "trigger", "hintFrac", "lineAlpha", "steps", "bands",
        "minView", "markInset", "borderRadio", "compactW", "thumbPad", "minDraw", "trackAlpha", "minDragW",
        "selectionRadius", "ribbonTop", "ribbonH", "handleRadius", "trackInset", "padV", "cap",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<size_t>(UiStyleKey::Count));
    return kNames[static_cast<size_t>(key)];
}

} // namespace burnhope
