#include "ui/Template.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/Check.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/State.hpp"
#include "ui/StyleInternal.hpp"
#include "ui/StyleSheet.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace burnhope {

// Names a blueprint may list in "traits", with the function that applies each to a freshly spawned node.
struct UiTraitTable {
    char name[kUiTraitCap][16]{};
    UiTraitFn fn[kUiTraitCap]{};
    uint8_t count = 0;
};

namespace {

using json::Cur;

void addHoverable(UiState& s, uint16_t id) { s.ent[id].add<UiHoverable>(); }
void addFocusable(UiState& s, uint16_t id) { s.ent[id].add<UiFocusable>(); }
void addTextInput(UiState& s, uint16_t id) { s.ent[id].add<UiTextInput>(); }
void addNoHit(UiState& s, uint16_t id) { s.ent[id].add<UiNoHit>(); }
void addPass(UiState& s, uint16_t id) { s.ent[id].add<UiPass>(); }

bool addTrait(UiTraitTable& table, const char* name, UiTraitFn fn) {
    if (name == nullptr || name[0] == '\0' || std::strlen(name) >= sizeof(table.name[0])) {
        return false;
    }
    for (uint8_t i = 0; i < table.count; ++i) {
        if (std::strcmp(table.name[i], name) == 0) {
            return true;
        }
    }
    if (table.count >= kUiTraitCap) {
        return false;
    }
    std::strcpy(table.name[table.count], name);
    table.fn[table.count] = fn;
    ++table.count;
    return true;
}

UiTraitTable& traitTable(UiState& s) {
    UiTraitTable& table = s.world.ensure<UiTraitTable>();
    if (table.count == 0) {
        [[maybe_unused]] const bool seeded = addTrait(table, "hoverable", addHoverable) && addTrait(table, "focusable", addFocusable) &&
                            addTrait(table, "text_input", addTextInput) && addTrait(table, "pressable", nullptr) &&
                            addTrait(table, "draggable", nullptr) && addTrait(table, "nohit", addNoHit) && addTrait(table, "pass", addPass);
        BH_ASSERT(seeded, "built-in blueprint traits exceed the trait table");
    }
    return table;
}

// A part after its style class is laid over the blueprint's own flex block.
struct Resolved {
    UiFlex flex{};
    UiToken bg = UiToken::Custom;
    UiToken activeBg = UiToken::Custom;
    UiToken borderBg = UiToken::Custom;
    float alpha = -1.0f; // -1: the class leaves it to the token paint
    float borderW = -1.0f;
    float radius = -1.0f;
    float shadow = 0.0f;
    float borderDim = -1.0f; // border colour = fill colour times this; -1 keeps the paint's default
    float hoverAlpha = -1.0f; // >= 0: the node hovers to its own colour at this alpha
    float ramp = 0.0f;       // UiRamp of a colour-ramp panel (wheel, value bar, swatch)
    float iconLeft = -1.0f;   // >= 0: a labelled node draws its icon this far from the left edge, "icon" px wide
    float iconSide = 0.0f;
    uint8_t plate = 0;
};

// Lays the numeric keys of a style class over a part: sizes become px, the rest feeds the paint.
void mergeClass(Resolved& part, const UiStyleClass& cls) {
    UiFlex& flex = part.flex;
    auto get = [&](UiStyleKey key) { return cls.value[static_cast<size_t>(key)]; };
    auto has = [&](UiStyleKey key) { return uiStyleHas(&cls, key); };
    if (has(UiStyleKey::Height)) {
        flex.heightMode = UiSize::Px;
        flex.height = get(UiStyleKey::Height);
    }
    if (has(UiStyleKey::Width)) {
        flex.widthMode = UiSize::Px;
        flex.width = get(UiStyleKey::Width);
    }
    if (has(UiStyleKey::MaxW)) {
        flex.maxWMode = UiSize::Px;
        flex.maxW = get(UiStyleKey::MaxW);
    }
    if (has(UiStyleKey::MaxH)) {
        flex.maxHMode = UiSize::Px;
        flex.maxH = get(UiStyleKey::MaxH);
    }
    if (has(UiStyleKey::MinW)) {
        flex.minW = get(UiStyleKey::MinW);
    }
    if (has(UiStyleKey::MinH)) {
        flex.minH = get(UiStyleKey::MinH);
    }
    if (has(UiStyleKey::Pad)) {
        flex.pad = get(UiStyleKey::Pad);
    }
    if (has(UiStyleKey::PadL)) {
        flex.padL = get(UiStyleKey::PadL);
    }
    if (has(UiStyleKey::PadR)) {
        flex.padR = get(UiStyleKey::PadR);
    }
    if (has(UiStyleKey::Gap)) {
        flex.gap = get(UiStyleKey::Gap);
    }
    if (has(UiStyleKey::Margin)) {
        flex.marginL = flex.marginR = flex.marginT = flex.marginB = get(UiStyleKey::Margin);
    }
    if (has(UiStyleKey::MarginR)) {
        flex.marginR = get(UiStyleKey::MarginR);
    }
    if (cls.bg != UiToken::Custom) {
        part.bg = cls.bg;
    }
    if (cls.activeBg != UiToken::Custom) {
        part.activeBg = cls.activeBg;
    }
    if (cls.borderBg != UiToken::Custom) {
        part.borderBg = cls.borderBg;
    }
    if (has(UiStyleKey::Alpha)) {
        part.alpha = get(UiStyleKey::Alpha);
    }
    if (has(UiStyleKey::Border)) {
        part.borderW = get(UiStyleKey::Border);
    }
    if (has(UiStyleKey::Radius)) {
        part.radius = get(UiStyleKey::Radius);
    }
    if (has(UiStyleKey::Shadow)) {
        part.shadow = get(UiStyleKey::Shadow);
    }
    if (has(UiStyleKey::Plate)) {
        part.plate = get(UiStyleKey::Plate) != 0.0f ? 1 : 0;
    }
    if (has(UiStyleKey::BorderDim)) {
        part.borderDim = get(UiStyleKey::BorderDim);
    }
    if (has(UiStyleKey::HoverAlpha)) {
        part.hoverAlpha = get(UiStyleKey::HoverAlpha);
    }
    if (has(UiStyleKey::Ramp)) {
        part.ramp = get(UiStyleKey::Ramp);
    }
    if (has(UiStyleKey::IconLeft)) {
        part.iconLeft = get(UiStyleKey::IconLeft);
    }
    if (has(UiStyleKey::Icon)) {
        part.iconSide = get(UiStyleKey::Icon);
    }
}

// Merges the class `name` into `part`; false (part untouched) when neither the sheet nor the built-ins know it.
bool mergeNamed(const UiState& s, Resolved& part, const char* name) {
    UiStyleClass cls;
    if (!uiStyleResolve(s, name, cls)) {
        return false;
    }
    mergeClass(part, cls);
    return true;
}

UiPaint partPaint(const UiLook& look, const Resolved& part) {
    const float radius = part.radius == kUiRadiusTheme     ? look.radius
                         : part.radius == kUiRadiusThemeSm ? look.radiusSm
                         : part.radius >= 0.0f             ? part.radius
                         : part.bg != UiToken::Custom      ? std::max(tokenRadius(look, part.bg), 0.0f)
                                                           : 0.0f;
    UiPaint paint = part.bg != UiToken::Custom ? paintToken(look, part.bg, radius) : paintClear();
    paint.radius = radius;
    if (part.alpha >= 0.0f) {
        paint.a = part.alpha;
    }
    if (part.borderW >= 0.0f) {
        paint.borderW = part.borderW;
    }
    paint.shadow = part.shadow;
    paint.plate = part.plate;
    paint.ramp = static_cast<UiRamp>(static_cast<uint8_t>(part.ramp));
    if (part.borderBg != UiToken::Custom) {
        tokenRgb(look, part.borderBg, paint.br, paint.bg, paint.bb);
    }
    if (part.borderDim >= 0.0f) {
        paint.br = paint.r * part.borderDim;
        paint.bg = paint.g * part.borderDim;
        paint.bb = paint.b * part.borderDim;
    }
    if (part.iconLeft >= 0.0f) {
        paint.iconLeft = static_cast<uint8_t>(part.iconLeft);
        paint.iconSide = static_cast<uint8_t>(part.iconSide);
    }
    if (part.hoverAlpha >= 0.0f) {
        paint.hr = paint.r;
        paint.hg = paint.g;
        paint.hb = paint.b;
        paint.ha = part.hoverAlpha;
    }
    paint.idleToken = part.bg;
    paint.activeToken = part.activeBg;
    return paint;
}

struct Load {
    UiState* state = nullptr;
    const char* doc = nullptr;
    const char* text = nullptr;
    bool failed = false;
    UiTemplateSheet* sheet = nullptr;
    UiTraitTable* traits = nullptr;
};

bool fail(Load& load, const char* at, const char* message, const char* detail = "") {
    if (load.failed) {
        return false;
    }
    load.failed = true;
    uint32_t line = 1;
    uint32_t col = 1;
    if (load.text != nullptr && at != nullptr && at >= load.text) {
        for (const char* p = load.text; p < at; ++p) {
            if (*p == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
    }
    spdlog::error("{}:{}:{}: {} {}", load.doc != nullptr ? load.doc : "template", line, col, message, detail);
    return false;
}

// Reads `{ "key": value, ... }`, calling visit(key, keyAt, valueAt) for each member; visit consumes the value.
template <class Visit>
bool eachMember(Load& load, Cur& c, Visit&& visit) {
    if (!json::eat(c, '{')) {
        return fail(load, c.p, "expected an object");
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        json::skip(c);
        const char* keyAt = c.p;
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return fail(load, keyAt, "expected a key of at most 15 characters");
        }
        json::skip(c);
        if (!visit(static_cast<const char*>(key), keyAt, c.p)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return fail(load, c.p, "expected , or } after the value of", key);
        }
    }
}

// The index of the word at `c` in `words`, or -1 (a bad word or a non-string).
int wordOf(Cur& c, std::initializer_list<const char*> words) {
    char text[16]{};
    uint32_t n = 0;
    if (!json::parseStringCap(c, text, sizeof(text), n)) {
        return -1;
    }
    int index = 0;
    for (const char* word : words) {
        if (std::strcmp(text, word) == 0) {
            return index;
        }
        ++index;
    }
    return -1;
}

bool boolOf(Cur& c, bool& out) {
    json::skip(c);
    if (static_cast<size_t>(c.end - c.p) >= 4 && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        out = true;
        return true;
    }
    if (static_cast<size_t>(c.end - c.p) >= 5 && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        out = false;
        return true;
    }
    return false;
}

bool parseFlexBlock(Load& load, Cur& c, UiTplPart& part, bool root) {
    UiFlex& flex = part.flex;
    return eachMember(load, c, [&](const char* key, const char* keyAt, const char* valueAt) {
        auto number = [&](float& dst) {
            double v = 0.0;
            if (!json::parseNumber(c, v)) {
                return fail(load, valueAt, "bad number for flex key", key);
            }
            dst = static_cast<float>(v);
            return true;
        };
        auto word = [&](std::initializer_list<const char*> words) {
            const int index = wordOf(c, words);
            if (index < 0) {
                fail(load, valueAt, "bad word for flex key", key);
            }
            return index;
        };
        if (std::strcmp(key, "dir") == 0) {
            const int w = word({"column", "row", "col", "column-reverse", "row-reverse"});
            static constexpr UiFlexDir kDirs[] = {UiFlexDir::Column, UiFlexDir::Row, UiFlexDir::Column, UiFlexDir::ColumnReverse, UiFlexDir::RowReverse};
            if (w >= 0) {
                flex.direction = kDirs[w];
            }
            return w >= 0;
        }
        if (std::strcmp(key, "align") == 0) {
            const int w = word({"stretch", "center", "start", "end", "baseline"});
            if (w >= 0) {
                flex.align = static_cast<UiAlign>(w);
            }
            return w >= 0;
        }
        if (std::strcmp(key, "justify") == 0) {
            const int w = word({"start", "center", "end", "between", "around", "evenly"});
            if (w >= 0) {
                flex.justify = static_cast<UiJustify>(w);
            }
            return w >= 0;
        }
        if (std::strcmp(key, "wrap") == 0) {
            bool on = false;
            if (!boolOf(c, on)) {
                return fail(load, valueAt, "wrap must be true or false");
            }
            flex.wrap = on ? UiFlexWrap::Wrap : UiFlexWrap::NoWrap;
            return true;
        }
        if (std::strcmp(key, "gap") == 0) {
            return number(flex.gap);
        }
        if (std::strcmp(key, "shrink") == 0) {
            return number(flex.shrink);
        }
        // A part below the root is placed by its own blueprint, so it may state how it sits in its parent.
        if (!root && std::strcmp(key, "grow") == 0) {
            return number(flex.grow);
        }
        if (!root && std::strcmp(key, "self") == 0) {
            const int w = word({"auto", "stretch", "center", "start", "end", "baseline"});
            if (w >= 0) {
                flex.self = static_cast<UiAlignSelf>(w);
            }
            return w >= 0;
        }
        if (!root && (std::strcmp(key, "wp") == 0 || std::strcmp(key, "hp") == 0)) {
            const bool wide = key[0] == 'w';
            float percent = 0.0f;
            if (!number(percent)) {
                return false;
            }
            (wide ? flex.widthMode : flex.heightMode) = UiSize::Percent;
            (wide ? flex.width : flex.height) = percent;
            return true;
        }
        if (std::strcmp(key, "pad") == 0) {
            return number(flex.pad);
        }
        if (std::strcmp(key, "padL") == 0) {
            return number(flex.padL);
        }
        if (std::strcmp(key, "padR") == 0) {
            return number(flex.padR);
        }
        if (std::strcmp(key, "padT") == 0) {
            return number(flex.padT);
        }
        if (std::strcmp(key, "padB") == 0) {
            return number(flex.padB);
        }
        return fail(load, keyAt, "a blueprint root's flex may hold dir, align, justify, wrap, gap, shrink and pad* (parts below it also grow, self, wp, hp); sizes live in the style class and placement on the instance, not", key);
    });
}

bool parseTextBlock(Load& load, Cur& c, UiTplPart& part) {
    part.hasText = 1;
    return eachMember(load, c, [&](const char* key, const char* keyAt, const char* valueAt) {
        auto word = [&](std::initializer_list<const char*> words) {
            const int index = wordOf(c, words);
            if (index < 0) {
                fail(load, valueAt, "bad word for text key", key);
            }
            return index;
        };
        if (std::strcmp(key, "align") == 0) {
            const int w = word({"left", "center", "right", "justify"});
            part.textAlign = static_cast<uint8_t>(std::max(w, 0));
            return w >= 0;
        }
        if (std::strcmp(key, "valign") == 0) {
            const int w = word({"top", "center", "bottom"});
            part.textValign = static_cast<uint8_t>(std::max(w, 0));
            return w >= 0;
        }
        if (std::strcmp(key, "ellipsis") == 0) {
            const int w = word({"none", "end", "middle"});
            part.textEllipsis = static_cast<uint8_t>(std::max(w, 0));
            return w >= 0;
        }
        if (std::strcmp(key, "wrap") == 0) {
            bool on = false;
            if (!boolOf(c, on)) {
                return fail(load, valueAt, "wrap must be true or false");
            }
            part.textWrap = on ? 1 : 0;
            return true;
        }
        if (std::strcmp(key, "leading") == 0) {
            double v = 0.0;
            if (!json::parseNumber(c, v)) {
                return fail(load, valueAt, "leading must be a number");
            }
            part.textLeading = static_cast<float>(v);
            return true;
        }
        return fail(load, keyAt, "unknown text key", key);
    });
}

bool parseTraits(Load& load, Cur& c, UiTplPart& part) {
    if (!json::eat(c, '[')) {
        return fail(load, c.p, "traits must be an array of names");
    }
    json::skip(c);
    while (c.p < c.end && *c.p != ']') {
        const char* at = c.p;
        char name[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, name, sizeof(name), n)) {
            return fail(load, at, "a trait is a name of at most 15 characters");
        }
        // Overflow traits shape the Yoga node and need no component.
        if (std::strcmp(name, "clip") == 0) {
            part.flex.overflow = UiOverflow::Hidden;
        } else if (std::strcmp(name, "scroll") == 0) {
            part.flex.overflow = UiOverflow::Scroll;
        } else if (std::strcmp(name, "visible") == 0) {
            part.flex.overflow = UiOverflow::Visible;
        } else {
            uint8_t index = 0;
            while (index < load.traits->count && std::strcmp(load.traits->name[index], name) != 0) {
                ++index;
            }
            if (index == load.traits->count) {
                return fail(load, at, "unknown trait", name);
            }
            part.traits |= 1u << index;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, ']') || fail(load, c.p, "unterminated traits");
}

bool parsePart(Load& load, Cur& c, uint16_t parent, uint8_t depth, const char*& slotsAt);

bool parseParts(Load& load, Cur& c, uint16_t parent, uint8_t depth) {
    if (!json::eat(c, '[')) {
        return fail(load, c.p, "parts must be an array");
    }
    json::skip(c);
    while (c.p < c.end && *c.p != ']') {
        const char* unusedSlots = nullptr;
        if (!parsePart(load, c, parent, depth, unusedSlots)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, ']') || fail(load, c.p, "unterminated parts");
}

// Reads one part object, appends it, then its children, so parts stay in pre-order.
bool parsePart(Load& load, Cur& c, uint16_t parent, uint8_t depth, const char*& slotsAt) {
    if (depth >= kUiTplDepth) {
        return fail(load, c.p, "blueprint parts nest deeper than the depth cap");
    }
    UiTplPart part{};
    part.parent = parent;
    char cls[24]{};
    const char* clsAt = nullptr;
    const char* childrenAt = nullptr;
    uint32_t repeat = 1;
    bool seen = false;
    const bool ok = eachMember(load, c, [&](const char* key, const char* keyAt, const char* valueAt) {
        uint32_t n = 0;
        const bool first = !seen;
        seen = true;
        if (std::strcmp(key, "use") == 0) {
            char target[24]{};
            if (!first || !json::parseStringCap(c, target, sizeof(target), n)) {
                return fail(load, keyAt, "\"use\" must be the first key of a part and name a blueprint");
            }
            const UiTemplateSheet& sheet = *load.sheet;
            for (uint8_t i = 0; i < sheet.count; ++i) {
                if (std::strcmp(sheet.tpl[i].name, target) == 0) {
                    if (sheet.tpl[i].count != 1) {
                        return fail(load, valueAt, "only a one-part blueprint can be used:", target);
                    }
                    part = sheet.part[sheet.tpl[i].first];
                    part.parent = parent;
                    part.name[0] = '\0';
                    return true;
                }
            }
            return fail(load, valueAt, "\"use\" names a blueprint that is not defined before this one:", target);
        }
        if (std::strcmp(key, "count") == 0) {
            double v = 0.0;
            if (depth == 0 || !json::parseNumber(c, v) || v < 1.0 || v > 64.0) {
                return fail(load, valueAt, "count is a number from 1 to 64 and not allowed on the blueprint root");
            }
            repeat = static_cast<uint32_t>(v);
            return true;
        }
        if (std::strcmp(key, "role") == 0) {
            return json::parseStringCap(c, part.role, sizeof(part.role), n) || fail(load, valueAt, "role must be a name of at most 15 characters");
        }
        if (std::strcmp(key, "name") == 0) {
            return json::parseStringCap(c, part.name, sizeof(part.name), n) || fail(load, valueAt, "name must be a name of at most 15 characters");
        }
        if (std::strcmp(key, "class") == 0) {
            clsAt = valueAt;
            return json::parseStringCap(c, cls, sizeof(cls), n) || fail(load, valueAt, "class must be a name of at most 23 characters");
        }
        if (std::strcmp(key, "flex") == 0) {
            return parseFlexBlock(load, c, part, depth == 0);
        }
        if (std::strcmp(key, "text") == 0) {
            return parseTextBlock(load, c, part);
        }
        if (std::strcmp(key, "traits") == 0) {
            return parseTraits(load, c, part);
        }
        if (std::strcmp(key, "parts") == 0) {
            childrenAt = valueAt;
            return (valueAt < c.end && *valueAt == '[' && json::skipValue(c)) || fail(load, valueAt, "parts must be an array");
        }
        if (std::strcmp(key, "slots") == 0) {
            if (depth != 0) {
                return fail(load, keyAt, "only the blueprint root declares slots");
            }
            slotsAt = valueAt;
            return json::skipValue(c) || fail(load, valueAt, "slots must be an object");
        }
        return fail(load, keyAt, "unknown blueprint key", key);
    });
    if (!ok) {
        return false;
    }
    if (cls[0] != '\0') {
        UiStyleClass known;
        if (!uiStyleResolve(*load.state, cls, known)) {
            return fail(load, clsAt, "unknown style class", cls);
        }
        std::strcpy(part.cls, cls);
    }
    for (uint32_t copy = 0; copy < repeat; ++copy) {
        if (load.sheet->partCount >= kUiTplPartCap) {
            return fail(load, c.p, "blueprints exceed the part cap");
        }
        const uint16_t index = load.sheet->partCount++;
        load.sheet->part[index] = part;
        if (childrenAt != nullptr) {
            Cur inner{childrenAt, c.end};
            if (!parseParts(load, inner, index, static_cast<uint8_t>(depth + 1))) {
                return false;
            }
        }
    }
    return true;
}

bool parseSlots(Load& load, Cur& c, UiTemplate& tpl) {
    return eachMember(load, c, [&](const char* key, const char* keyAt, const char* valueAt) {
        char target[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, target, sizeof(target), n)) {
            return fail(load, valueAt, "a slot names the part it fills");
        }
        if (tpl.slotCount >= kUiTplSlotCap || std::strlen(key) >= sizeof(tpl.slot[0].name)) {
            return fail(load, keyAt, "too many slots, or a slot name over 11 characters:", key);
        }
        for (uint16_t i = 0; i < tpl.count; ++i) {
            if (std::strcmp(load.sheet->part[tpl.first + i].name, target) == 0) {
                UiTplSlot& slot = tpl.slot[tpl.slotCount++];
                std::strcpy(slot.name, key);
                slot.part = i;
                return true;
            }
        }
        return fail(load, valueAt, "slot names a part that does not exist:", target);
    });
}

bool parseTemplate(Load& load, const UiTemplateSource& source) {
    load.doc = source.name;
    load.text = source.text.data();
    UiTemplateSheet& sheet = *load.sheet;
    if (sheet.count >= kUiTemplateCap) {
        return fail(load, load.text, "blueprints exceed the template cap");
    }
    if (source.name == nullptr || std::strlen(source.name) >= sizeof(sheet.tpl[0].name)) {
        return fail(load, load.text, "a blueprint name has at most 23 characters");
    }
    for (uint8_t i = 0; i < sheet.count; ++i) {
        if (std::strcmp(sheet.tpl[i].name, source.name) == 0) {
            return fail(load, load.text, "duplicate blueprint", source.name);
        }
    }
    UiTemplate tpl{};
    std::strcpy(tpl.name, source.name);
    tpl.nameHash = style::nameHash(source.name);
    tpl.first = sheet.partCount;
    Cur c{source.text.data(), source.text.data() + source.text.size()};
    const char* slotsAt = nullptr;
    if (!parsePart(load, c, kUiNone, 0, slotsAt)) {
        return false;
    }
    tpl.count = static_cast<uint16_t>(sheet.partCount - tpl.first);
    if (slotsAt != nullptr) {
        Cur slots{slotsAt, c.end};
        if (!parseSlots(load, slots, tpl)) {
            return false;
        }
    }
    sheet.tpl[sheet.count++] = tpl;
    return true;
}

} // namespace

bool uiTemplateTrait(UiState& s, const char* name, UiTraitFn fn) { return addTrait(traitTable(s), name, fn); }

bool uiTemplatesLoad(UiState& s, std::span<const UiTemplateSource> sources) {
    UiTemplateSheet sheet{};
    Load load{.state = &s, .sheet = &sheet, .traits = &traitTable(s)};
    for (const UiTemplateSource& source : sources) {
        if (!parseTemplate(load, source)) {
            return false;
        }
    }
    s.world.ensure<UiTemplateSheet>() = sheet;
    return true;
}

bool uiTemplatesLoadAsset(UiState& s, const char* manifest) {
    char path[512]{};
    if (manifest == nullptr || !platformResolveAsset(manifest, path, sizeof(path))) {
        spdlog::error("template manifest {} was not found", manifest != nullptr ? manifest : "");
        return false;
    }
    MappedFile list{};
    if (!fileMapReadOnly(path, &list) || list.data == nullptr) {
        spdlog::error("template manifest {} did not map", path);
        return false;
    }
    char names[kUiTemplateCap][24]{};
    uint8_t count = 0;
    bool ok = false;
    {
        Load load{.doc = manifest, .text = reinterpret_cast<const char*>(list.data)};
        Cur c{reinterpret_cast<const char*>(list.data), reinterpret_cast<const char*>(list.data) + list.size};
        ok = eachMember(load, c, [&](const char* key, const char* keyAt, const char* valueAt) {
            if (std::strcmp(key, "templates") != 0) {
                return fail(load, keyAt, "a template manifest holds only \"templates\", not", key);
            }
            if (!json::eat(c, '[')) {
                return fail(load, valueAt, "templates must be an array of names");
            }
            json::skip(c);
            while (c.p < c.end && *c.p != ']') {
                uint32_t n = 0;
                if (count >= kUiTemplateCap || !json::parseStringCap(c, names[count], sizeof(names[count]), n)) {
                    return fail(load, c.p, "a template name has at most 23 characters, and the manifest lists at most 32");
                }
                ++count;
                json::skip(c);
                if (c.p < c.end && *c.p == ',') {
                    ++c.p;
                    json::skip(c);
                }
            }
            return json::eat(c, ']') || fail(load, c.p, "unterminated templates");
        });
    }
    fileUnmap(&list);
    if (!ok) {
        return false;
    }
    MappedFile files[kUiTemplateCap]{};
    UiTemplateSource sources[kUiTemplateCap]{};
    uint8_t mapped = 0;
    bool loaded = true;
    for (uint8_t i = 0; i < count && loaded; ++i) {
        char asset[160];
        std::snprintf(asset, sizeof(asset), "assets/ui/%.23s.json", names[i]);
        loaded = platformResolveAsset(asset, path, sizeof(path)) && fileMapReadOnly(path, &files[i]) && files[i].data != nullptr;
        if (!loaded) {
            spdlog::error("template {} was not found or did not map", asset);
            break;
        }
        ++mapped;
        sources[i] = {.name = names[i], .text = {reinterpret_cast<const char*>(files[i].data), files[i].size}};
    }
    const bool result = loaded && uiTemplatesLoad(s, std::span<const UiTemplateSource>(sources, count));
    for (uint8_t i = 0; i < mapped; ++i) {
        fileUnmap(&files[i]);
    }
    return result;
}

UiTemplateId uiTemplateFind(const UiState& s, const char* name) {
    const UiTemplateSheet* sheet = s.world.try_get<UiTemplateSheet>();
    if (sheet == nullptr || name == nullptr) {
        return kUiTemplateNone;
    }
    const uint32_t hash = style::nameHash(name);
    for (uint8_t i = 0; i < sheet->count; ++i) {
        if (sheet->tpl[i].nameHash == hash && std::strcmp(sheet->tpl[i].name, name) == 0) {
            return i;
        }
    }
    return kUiTemplateNone;
}

namespace {

// The part with its own class, and for the root `over` laid on top.
Resolved resolvePart(const UiState& s, const UiTemplate& tpl, const UiTplPart& part, const char* over) {
    Resolved resolved{.flex = part.flex};
    if (part.cls[0] != '\0' && !mergeNamed(s, resolved, part.cls)) {
        spdlog::error("blueprint {} names the style class {}, which is no longer defined", tpl.name, part.cls);
    }
    if (over != nullptr) {
        (void)mergeNamed(s, resolved, over);
    }
    return resolved;
}

void styleText(UiState& s, uint16_t node, const UiTplPart& part) {
    if (part.hasText == 0) {
        return;
    }
    UiText text{};
    if (const UiText* live = s.ent[node].try_get<UiText>()) {
        text = *live;
    }
    text.align = static_cast<UiTextAlign>(part.textAlign);
    text.valign = static_cast<UiTextValign>(part.textValign);
    text.ellipsis = static_cast<UiTextEllipsis>(part.textEllipsis);
    text.wrap = part.textWrap != 0;
    text.leading = part.textLeading;
    s.ent[node].set<UiText>(text);
}

void applyTraits(UiState& s, uint16_t node, const UiTplPart& part) {
    const UiTraitTable* traits = s.world.try_get<UiTraitTable>();
    for (uint8_t bit = 0; traits != nullptr && bit < traits->count; ++bit) {
        if ((part.traits & (1u << bit)) != 0 && traits->fn[bit] != nullptr) {
            traits->fn[bit](s, node);
        }
    }
}

bool overrideKnown(const UiState& s, const char* cls) {
    if (cls == nullptr) {
        return true;
    }
    UiStyleClass probe;
    if (!uiStyleResolve(s, cls, probe)) {
        spdlog::error("unknown style class {}", cls);
        return false;
    }
    return true;
}

} // namespace

uint16_t uiSpawnTemplate(UiState& s, uint16_t parent, UiTemplateId id, const char* cls) {
    const UiTemplateSheet* sheet = s.world.try_get<UiTemplateSheet>();
    if (sheet == nullptr || id >= sheet->count) {
        BH_ASSERT(false, "uiSpawnTemplate: unknown blueprint id");
        return kUiNone;
    }
    const UiTemplate& tpl = sheet->tpl[id];
    if (static_cast<uint32_t>(s.count) + tpl.count > kUiCap || !overrideKnown(s, cls)) {
        return kUiNone;
    }
    uint16_t ids[kUiTplPartCap];
    for (uint16_t i = 0; i < tpl.count; ++i) {
        const UiTplPart& part = sheet->part[tpl.first + i];
        const Resolved resolved = resolvePart(s, tpl, part, i == 0 ? cls : nullptr);
        const uint16_t at = part.parent == kUiNone ? parent : ids[part.parent - tpl.first];
        const uint16_t node = spawn(s, at, resolved.flex, partPaint(s.look, resolved));
        ids[i] = node;
        styleText(s, node, part);
        applyTraits(s, node, part);
    }
    s.layoutDirty = true;
    s.visualDirty = true;
    return ids[0];
}

bool uiApplyTemplate(UiState& s, uint16_t id, UiTemplateId tplId, const char* cls) {
    const UiTemplateSheet* sheet = s.world.try_get<UiTemplateSheet>();
    if (sheet == nullptr || tplId >= sheet->count || id >= s.count || !overrideKnown(s, cls)) {
        return false;
    }
    const UiTemplate& tpl = sheet->tpl[tplId];
    BH_ASSERT(tpl.count == 1, "uiApplyTemplate restyles one node: use a one-part blueprint");
    const UiTplPart& part = sheet->part[tpl.first];
    const Resolved resolved = resolvePart(s, tpl, part, cls);
    s.ent[id].set<UiFlex>(resolved.flex);
    s.ent[id].set<UiPaint>(partPaint(s.look, resolved));
    styleText(s, id, part);
    applyTraits(s, id, part);
    s.layoutDirty = true;
    s.visualDirty = true;
    return true;
}

uint16_t uiSpawnTemplate(UiState& s, uint16_t parent, const char* name, const char* cls) {
    const UiTemplateId id = uiTemplateFind(s, name);
    if (id == kUiTemplateNone) {
        spdlog::error("uiSpawnTemplate: no blueprint named {}", name != nullptr ? name : "");
        return kUiNone;
    }
    return uiSpawnTemplate(s, parent, id, cls);
}

uint16_t uiTemplateSlot(const UiState& s, UiTemplateId id, uint16_t root, const char* slot) {
    const UiTemplateSheet* sheet = s.world.try_get<UiTemplateSheet>();
    if (sheet == nullptr || id >= sheet->count || slot == nullptr || root >= s.count) {
        return kUiNone;
    }
    const UiTemplate& tpl = sheet->tpl[id];
    for (uint8_t i = 0; i < tpl.slotCount; ++i) {
        if (std::strcmp(tpl.slot[i].name, slot) == 0) {
            return static_cast<uint16_t>(root + tpl.slot[i].part);
        }
    }
    return kUiNone;
}

} // namespace burnhope
