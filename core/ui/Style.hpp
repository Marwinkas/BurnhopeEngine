#pragma once

#include <yoga/Yoga.h>

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

enum UiStyleMask : uint32_t {
    kUiStyleBg = 1u << 0,
    kUiStyleBorderColor = 1u << 1,
    kUiStyleText = 1u << 2,
    kUiStylePadding = 1u << 3,
    kUiStyleMargin = 1u << 4,
    kUiStyleRadius = 1u << 5,
    kUiStyleBorderWidth = 1u << 6,
    kUiStyleBlur = 1u << 7,
    kUiStyleOpacity = 1u << 8,
    kUiStyleWidth = 1u << 9,
    kUiStyleHeight = 1u << 10,
};

struct UiStyle {
    uint32_t bg_color = 0;
    uint32_t border_color = 0;
    uint32_t text_color = 0;
    uint32_t flags = 0;
    float padding[4]{};
    float margin[4]{};
    float radius[4]{};
    float border_width = 0.0f;
    float blur = 0.0f;
    float opacity = 1.0f;
    float width = 0.0f;
    float height = 0.0f;
    float reserved[3]{};
};

struct StyleRule {
    uint32_t classHash = 0;
    uint32_t mask = 0;
    uint16_t priority = 0;
    uint16_t reserved0 = 0;
    uint32_t reserved1 = 0;
    UiStyle style{};
};

struct StyleSheet {
    const StyleRule* rules = nullptr;
    uint32_t count = 0;
};

static_assert(std::is_trivially_copyable_v<UiStyle>);
static_assert(sizeof(UiStyle) % 16 == 0);
static_assert(std::is_trivially_copyable_v<StyleRule>);
static_assert(sizeof(StyleRule) % 16 == 0);

constexpr uint32_t uiClassHash(const char* name) {
    uint32_t hash = 2166136261u;
    if (name == nullptr) {
        return hash;
    }
    for (const char* p = name; *p != '\0'; ++p) {
        hash ^= static_cast<uint32_t>(static_cast<unsigned char>(*p));
        hash *= 16777619u;
    }
    return hash;
}

inline UiStyle uiStyleDefault() {
    UiStyle style{};
    style.opacity = 1.0f;
    return style;
}

inline void uiStyleCopyField(UiStyle& dst, const UiStyle& src, uint32_t bit) {
    if (bit == kUiStyleBg) {
        dst.bg_color = src.bg_color;
    } else if (bit == kUiStyleBorderColor) {
        dst.border_color = src.border_color;
    } else if (bit == kUiStyleText) {
        dst.text_color = src.text_color;
    } else if (bit == kUiStylePadding) {
        std::memcpy(dst.padding, src.padding, sizeof(dst.padding));
    } else if (bit == kUiStyleMargin) {
        std::memcpy(dst.margin, src.margin, sizeof(dst.margin));
    } else if (bit == kUiStyleRadius) {
        std::memcpy(dst.radius, src.radius, sizeof(dst.radius));
    } else if (bit == kUiStyleBorderWidth) {
        dst.border_width = src.border_width;
    } else if (bit == kUiStyleBlur) {
        dst.blur = src.blur;
    } else if (bit == kUiStyleOpacity) {
        dst.opacity = src.opacity;
    } else if (bit == kUiStyleWidth) {
        dst.width = src.width;
    } else if (bit == kUiStyleHeight) {
        dst.height = src.height;
    }
}

inline void uiResolveStyle(const StyleSheet& sheet, const uint32_t* classHashes, uint32_t count, UiStyle* outStyle) {
    if (outStyle == nullptr) {
        return;
    }
    *outStyle = uiStyleDefault();
    if (sheet.rules == nullptr || classHashes == nullptr) {
        return;
    }
    uint16_t won[11]{};
    for (uint32_t r = 0; r < sheet.count; ++r) {
        const StyleRule& rule = sheet.rules[r];
        bool hit = false;
        for (uint32_t c = 0; c < count; ++c) {
            if (classHashes[c] == rule.classHash) {
                hit = true;
                break;
            }
        }
        if (!hit) {
            continue;
        }
        uint32_t bit = 1u;
        for (uint32_t slot = 0; slot < 11; ++slot, bit <<= 1) {
            if ((rule.mask & bit) == 0 || rule.priority < won[slot]) {
                continue;
            }
            uiStyleCopyField(*outStyle, rule.style, bit);
            outStyle->flags |= bit;
            won[slot] = rule.priority;
        }
    }
}

inline void uiStyleApplyYoga(YGNodeRef node, const UiStyle& style) {
    if (node == nullptr) {
        return;
    }
    if ((style.flags & kUiStylePadding) != 0) {
        YGNodeStyleSetPadding(node, YGEdgeTop, style.padding[0]);
        YGNodeStyleSetPadding(node, YGEdgeRight, style.padding[1]);
        YGNodeStyleSetPadding(node, YGEdgeBottom, style.padding[2]);
        YGNodeStyleSetPadding(node, YGEdgeLeft, style.padding[3]);
    }
    if ((style.flags & kUiStyleMargin) != 0) {
        YGNodeStyleSetMargin(node, YGEdgeTop, style.margin[0]);
        YGNodeStyleSetMargin(node, YGEdgeRight, style.margin[1]);
        YGNodeStyleSetMargin(node, YGEdgeBottom, style.margin[2]);
        YGNodeStyleSetMargin(node, YGEdgeLeft, style.margin[3]);
    }
    if ((style.flags & kUiStyleWidth) != 0) {
        YGNodeStyleSetWidth(node, style.width);
    }
    if ((style.flags & kUiStyleHeight) != 0) {
        YGNodeStyleSetHeight(node, style.height);
    }
}

} // namespace burnhope
