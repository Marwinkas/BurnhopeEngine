#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr float kUiGapUnset = -1.0f;

enum class UiSize : uint8_t {
    Auto = 0,
    Px = 1,
    Percent = 2,
};

enum class UiFlexDir : uint8_t {
    Column = 0,
    Row = 1,
    ColumnReverse = 2,
    RowReverse = 3,
};

enum class UiAlign : uint8_t {
    Stretch = 0,
    Center = 1,
    Start = 2,
    End = 3,
    Baseline = 4,
};

enum class UiJustify : uint8_t {
    Start = 0,
    Center = 1,
    End = 2,
    Between = 3,
    Around = 4,
    Evenly = 5,
};

enum class UiPosition : uint8_t {
    Static = 0,
    Absolute = 1,
    Relative = 2,
};

enum class UiOverflow : uint8_t {
    Visible = 0,
    Hidden = 1,
    Scroll = 2,
};

enum class UiFlexDrop : uint8_t {
    None = 0,
    HideLast = 1,
    HideFirst = 2,
};

enum class UiFlexWrap : uint8_t {
    NoWrap = 0,
    Wrap = 1,
    WrapReverse = 2,
};

enum class UiAlignContent : uint8_t {
    Stretch = 0,
    Center = 1,
    Start = 2,
    End = 3,
    Between = 4,
    Around = 5,
    Evenly = 6,
};

enum class UiAlignSelf : uint8_t {
    Auto = 0,
    Stretch = 1,
    Center = 2,
    Start = 3,
    End = 4,
    Baseline = 5,
};

enum class UiBoxSizing : uint8_t {
    BorderBox = 0,
    ContentBox = 1,
};

enum class UiInsetFlags : uint8_t {
    None   = 0,
    Right  = 1 << 0,
    Bottom = 1 << 1,
};

inline constexpr UiInsetFlags operator|(UiInsetFlags a, UiInsetFlags b) noexcept {
    return static_cast<UiInsetFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr bool operator&(UiInsetFlags a, UiInsetFlags b) noexcept {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// Компонент верстки: Yoga читает эти поля и строит дерево коробок
struct UiFlex {
    UiFlexDir direction = UiFlexDir::Column;
    UiAlign align = UiAlign::Stretch;
    UiJustify justify = UiJustify::Start;
    UiSize widthMode = UiSize::Auto;
    UiSize heightMode = UiSize::Auto;
    float width = 0.0f;
    float height = 0.0f;
    float grow = 0.0f;
    float shrink = 1.0f;
    float pad = 0.0f;
    float padL = 0.0f;
    float padR = 0.0f;
    float padT = 0.0f;
    float padB = 0.0f;
    float gap = 0.0f;
    float minW = 0.0f;
    float minH = 0.0f;
    float marginL = 0.0f;
    float marginR = 0.0f;
    float marginT = 0.0f;
    float marginB = 0.0f;
    UiPosition position = UiPosition::Static;
    UiFlexDrop drop = UiFlexDrop::None;
    float posX = 0.0f;
    float posY = 0.0f;
    UiFlexWrap wrap = UiFlexWrap::NoWrap;
    UiAlignContent content = UiAlignContent::Stretch;
    UiAlignSelf self = UiAlignSelf::Auto;
    UiSize basisMode = UiSize::Auto;
    UiSize maxWMode = UiSize::Auto;
    UiSize maxHMode = UiSize::Auto;
    UiOverflow overflow = UiOverflow::Visible;
    UiBoxSizing box = UiBoxSizing::BorderBox;
    UiInsetFlags inset = UiInsetFlags::None;
    float basis = 0.0f;
    float maxW = 0.0f;
    float maxH = 0.0f;
    float gapRow = kUiGapUnset;
    float aspect = 0.0f;
    float posR = 0.0f;
    float posB = 0.0f;

    [[nodiscard]] float effectiveRowGap() const noexcept {
        return gapRow < 0.0f ? gap : gapRow;
    }
};

static_assert(std::is_trivially_copyable_v<UiFlex>);

} 