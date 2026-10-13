#include "core/ui/FlexAttr.hpp"
#include <cstring>

namespace burnhope {

void uiFlexApplyAttr(UiFlex& flex, const UiFlexAttr& attr) noexcept {
    flex.direction = static_cast<UiFlexDir>(attr.direction);
    flex.wrap = static_cast<UiFlexWrap>(attr.wrap);
    flex.justify = static_cast<UiJustify>(attr.justify);
    flex.align = static_cast<UiAlign>(attr.align);
    flex.widthMode = static_cast<UiSize>(attr.widthMode);
    flex.heightMode = static_cast<UiSize>(attr.heightMode);
    flex.width = attr.width;
    flex.height = attr.height;
    flex.grow = attr.grow;
    flex.shrink = attr.shrink;
    flex.pad = attr.pad;
    flex.padL = attr.padL;
    flex.padR = attr.padR;
    flex.padT = attr.padT;
    flex.padB = attr.padB;
    flex.gap = attr.gap;
    flex.position = static_cast<UiPosition>(attr.position);
    flex.posX = attr.posX;
    flex.posY = attr.posY;
    flex.minW = attr.minW;
    flex.minH = attr.minH;
    flex.marginL = attr.marginL;
    flex.marginR = attr.marginR;
    flex.marginT = attr.marginT;
    flex.marginB = attr.marginB;
    flex.drop = static_cast<UiFlexDrop>(attr.drop);
    flex.content = static_cast<UiAlignContent>(attr.content);
    flex.self = static_cast<UiAlignSelf>(attr.self);
    flex.basisMode = static_cast<UiSize>(attr.basisMode);
    flex.maxWMode = static_cast<UiSize>(attr.maxWMode);
    flex.maxHMode = static_cast<UiSize>(attr.maxHMode);
    flex.overflow = static_cast<UiOverflow>(attr.overflow);
    flex.box = static_cast<UiBoxSizing>(attr.box);
    flex.inset = static_cast<UiInsetFlags>(attr.inset);
    flex.basis = attr.basis;
    flex.maxW = attr.maxW;
    flex.maxH = attr.maxH;
    flex.gapRow = attr.gapRow;
    flex.aspect = attr.aspect;
    flex.posR = attr.posR;
    flex.posB = attr.posB;
}

bool uiFlexApplyAttr(UiFlex& flex, const char* key, const char* word) noexcept {
    if (key == nullptr || word == nullptr) {
        return false;
    }
    if (std::strcmp(key, "dir") == 0) {
        if (std::strcmp(word, "row") == 0) {
            flex.direction = UiFlexDir::Row;
        } else if (std::strcmp(word, "column-reverse") == 0) {
            flex.direction = UiFlexDir::ColumnReverse;
        } else if (std::strcmp(word, "row-reverse") == 0) {
            flex.direction = UiFlexDir::RowReverse;
        } else {
            flex.direction = UiFlexDir::Column;
        }
        return true;
    }
    if (std::strcmp(key, "wrap") == 0) {
        if (std::strcmp(word, "wrap") == 0) {
            flex.wrap = UiFlexWrap::Wrap;
        } else if (std::strcmp(word, "reverse") == 0) {
            flex.wrap = UiFlexWrap::WrapReverse;
        } else {
            flex.wrap = UiFlexWrap::NoWrap;
        }
        return true;
    }
    if (std::strcmp(key, "justify") == 0) {
        if (std::strcmp(word, "center") == 0) {
            flex.justify = UiJustify::Center;
        } else if (std::strcmp(word, "end") == 0) {
            flex.justify = UiJustify::End;
        } else if (std::strcmp(word, "between") == 0) {
            flex.justify = UiJustify::Between;
        } else if (std::strcmp(word, "around") == 0) {
            flex.justify = UiJustify::Around;
        } else if (std::strcmp(word, "evenly") == 0) {
            flex.justify = UiJustify::Evenly;
        } else {
            flex.justify = UiJustify::Start;
        }
        return true;
    }
    if (std::strcmp(key, "align") == 0) {
        if (std::strcmp(word, "center") == 0) {
            flex.align = UiAlign::Center;
        } else if (std::strcmp(word, "start") == 0) {
            flex.align = UiAlign::Start;
        } else if (std::strcmp(word, "end") == 0) {
            flex.align = UiAlign::End;
        } else if (std::strcmp(word, "baseline") == 0) {
            flex.align = UiAlign::Baseline;
        } else {
            flex.align = UiAlign::Stretch;
        }
        return true;
    }
    if (std::strcmp(key, "content") == 0) {
        if (std::strcmp(word, "center") == 0) {
            flex.content = UiAlignContent::Center;
        } else if (std::strcmp(word, "start") == 0) {
            flex.content = UiAlignContent::Start;
        } else if (std::strcmp(word, "end") == 0) {
            flex.content = UiAlignContent::End;
        } else if (std::strcmp(word, "between") == 0) {
            flex.content = UiAlignContent::Between;
        } else if (std::strcmp(word, "around") == 0) {
            flex.content = UiAlignContent::Around;
        } else if (std::strcmp(word, "evenly") == 0) {
            flex.content = UiAlignContent::Evenly;
        } else {
            flex.content = UiAlignContent::Stretch;
        }
        return true;
    }
    if (std::strcmp(key, "self") == 0) {
        if (std::strcmp(word, "stretch") == 0) {
            flex.self = UiAlignSelf::Stretch;
        } else if (std::strcmp(word, "center") == 0) {
            flex.self = UiAlignSelf::Center;
        } else if (std::strcmp(word, "start") == 0) {
            flex.self = UiAlignSelf::Start;
        } else if (std::strcmp(word, "end") == 0) {
            flex.self = UiAlignSelf::End;
        } else if (std::strcmp(word, "baseline") == 0) {
            flex.self = UiAlignSelf::Baseline;
        } else {
            flex.self = UiAlignSelf::Auto;
        }
        return true;
    }
    if (std::strcmp(key, "overflow") == 0) {
        if (std::strcmp(word, "hidden") == 0) {
            flex.overflow = UiOverflow::Hidden;
        } else if (std::strcmp(word, "scroll") == 0) {
            flex.overflow = UiOverflow::Scroll;
        } else {
            flex.overflow = UiOverflow::Visible;
        }
        return true;
    }
    if (std::strcmp(key, "box") == 0) {
        flex.box = (std::strcmp(word, "content") == 0) ? UiBoxSizing::ContentBox : UiBoxSizing::BorderBox;
        return true;
    }
    if (std::strcmp(key, "position") == 0) {
        if (std::strcmp(word, "absolute") == 0) {
            flex.position = UiPosition::Absolute;
        } else if (std::strcmp(word, "relative") == 0) {
            flex.position = UiPosition::Relative;
        } else {
            flex.position = UiPosition::Static;
        }
        return true;
    }
    return false;
}

bool uiFlexApplyAttr(UiFlex& flex, const char* key, float value) noexcept {
    if (key == nullptr) {
        return false;
    }
    if (std::strcmp(key, "grow") == 0) {
        flex.grow = value;
        flex.shrink = 1.0f;
        return true;
    }
    if (std::strcmp(key, "shrink") == 0) {
        flex.shrink = value;
        return true;
    }
    if (std::strcmp(key, "pad") == 0) {
        flex.pad = value;
        return true;
    }
    if (std::strcmp(key, "padL") == 0) {
        flex.padL = value;
        return true;
    }
    if (std::strcmp(key, "padR") == 0) {
        flex.padR = value;
        return true;
    }
    if (std::strcmp(key, "padT") == 0) {
        flex.padT = value;
        return true;
    }
    if (std::strcmp(key, "padB") == 0) {
        flex.padB = value;
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
        flex.inset = flex.inset | UiInsetFlags::Right;
        return true;
    }
    if (std::strcmp(key, "bottom") == 0) {
        flex.posB = value;
        flex.inset = flex.inset | UiInsetFlags::Bottom;
        return true;
    }
    return false;
}

void uiFlexApplyAttr(UiFlex& flex, const char* key, uint8_t mode, float value) noexcept {
    if (key == nullptr) {
        return;
    }
    const auto szMode = static_cast<UiSize>(mode);
    if (std::strcmp(key, "w") == 0) {
        flex.widthMode = szMode;
        flex.width = value;
    } else if (std::strcmp(key, "h") == 0) {
        flex.heightMode = szMode;
        flex.height = value;
    } else if (std::strcmp(key, "basis") == 0) {
        flex.basisMode = szMode;
        flex.basis = value;
    } else if (std::strcmp(key, "maxW") == 0) {
        flex.maxWMode = szMode;
        flex.maxW = value;
    } else if (std::strcmp(key, "maxH") == 0) {
        flex.maxHMode = szMode;
        flex.maxH = value;
    }
}

} // namespace burnhope