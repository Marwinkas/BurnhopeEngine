#include "style/GuiStyleSheet.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <span>

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/Check.hpp"

namespace burnhope::gui {
namespace {

using json::Cur;

constexpr uint64_t mask(GuiStyleBit bit) {
    return uint64_t{1} << bit;
}

// ---------------------------------------------------------------- palette

struct Rgb {
    float v[3];
};

constexpr Rgb kDefaultPalette[kTokCount] = {
    {{1.0f, 1.0f, 1.0f}},          // Custom
    {{0.055f, 0.055f, 0.067f}},    // Window     #0e0e11
    {{0.094f, 0.094f, 0.106f}},    // Title      #18181b
    {{0.094f, 0.094f, 0.106f}},    // Side       #18181b
    {{0.055f, 0.055f, 0.067f}},    // Content    #0e0e11
    {{0.133f, 0.133f, 0.153f}},    // Button     #222227
    {{0.075f, 0.075f, 0.086f}},    // Field
    {{0.094f, 0.094f, 0.106f}},    // Menu       #18181b
    {{0.55f, 0.28f, 0.28f}},       // Close
    {{0.98f, 0.98f, 0.98f}},       // Text
    {{0.631f, 0.631f, 0.667f}},    // Muted     #a1a1aa (>= 4.5:1 on the dark surfaces)
    {{0.133f, 0.133f, 0.153f}},    // Item       #222227
    {{0.976f, 0.451f, 0.086f}},    // Preview    #f97316
    {{0.055f, 0.055f, 0.067f}},    // Status
    {{0.133f, 0.133f, 0.153f}},    // Slider
    {{0.133f, 0.133f, 0.153f}},    // Check
    {{0.055f, 0.055f, 0.067f}},    // Scroll (follows Content)
    {{0.180f, 0.180f, 0.212f}},    // Chrome     #2e2e36
    {{0.976f, 0.451f, 0.086f}},    // Accent     #f97316
    {{0.976f, 0.451f, 0.086f}},    // Accent1
    {{0.600f, 0.270f, 0.060f}},    // Accent2
    {{0.976f, 0.451f, 0.086f}},    // ToolActive
    {{0.90f, 0.24f, 0.20f}},       // AxisX
    {{0.28f, 0.76f, 0.30f}},       // AxisY
    {{0.26f, 0.50f, 0.95f}},       // AxisZ
    {{0.75f, 0.75f, 0.78f}},       // Tool
    {{1.0f, 1.0f, 1.0f}},          // ToolHover
};

void resetLook(GuiStyleSheet& sheet) {
    std::memset(sheet.colors, 0, sizeof(sheet.colors));
    for (uint32_t i = 0; i < kTokCount; ++i) {
        sheet.colors[i][0] = kDefaultPalette[i].v[0];
        sheet.colors[i][1] = kDefaultPalette[i].v[1];
        sheet.colors[i][2] = kDefaultPalette[i].v[2];
        sheet.colors[i][3] = 1.0f;
    }
    sheet.themeRadius = 8.0f;
    sheet.themeRadiusSm = 6.0f;
    sheet.hover = 0.08f;
}

struct TokenName {
    const char* name;
    uint8_t id;
};

// Names accepted by "bg" / "activeBg" / "borderBg" ("dock" is the legacy alias of "content").
constexpr TokenName kTokenNames[] = {
    {"custom", kTokCustom}, {"window", kTokWindow}, {"dock", kTokContent}, {"title", kTokTitle}, {"side", kTokSide},
    {"content", kTokContent}, {"button", kTokButton}, {"field", kTokField}, {"menu", kTokMenu}, {"text", kTokText},
    {"close", kTokClose}, {"preview", kTokPreview}, {"status", kTokStatus}, {"chrome", kTokChrome}, {"slider", kTokSlider},
    {"check", kTokCheck}, {"scroll", kTokScroll}, {"muted", kTokMuted}, {"item", kTokItem}, {"accent", kTokAccent},
    {"accent0", kTokAccent}, {"accent1", kTokAccent1}, {"accent2", kTokAccent2}, {"toolActive", kTokToolActive},
    {"tool", kTokTool}, {"toolHover", kTokToolHover}, {"axisX", kTokAxisX}, {"axisY", kTokAxisY}, {"axisZ", kTokAxisZ},
};

bool tokenNamed(std::string_view name, uint8_t& out) {
    for (const TokenName& entry : kTokenNames) {
        if (name == entry.name) {
            out = entry.id;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- style fields

template <class S>
auto fslot(S& s, uint32_t bit) -> decltype(&s.w) {
    switch (bit) {
    case kSbW: return &s.w;
    case kSbH: return &s.h;
    case kSbMinW: return &s.minW;
    case kSbMinH: return &s.minH;
    case kSbMaxW: return &s.maxW;
    case kSbMaxH: return &s.maxH;
    case kSbPadL: return &s.pad[0];
    case kSbPadT: return &s.pad[1];
    case kSbPadR: return &s.pad[2];
    case kSbPadB: return &s.pad[3];
    case kSbMarL: return &s.margin[0];
    case kSbMarT: return &s.margin[1];
    case kSbMarR: return &s.margin[2];
    case kSbMarB: return &s.margin[3];
    case kSbGap: return &s.gap;
    case kSbPosL: return &s.pos[0];
    case kSbPosT: return &s.pos[1];
    case kSbPosR: return &s.pos[2];
    case kSbPosB: return &s.pos[3];
    case kSbGrow: return &s.grow;
    case kSbShrink: return &s.shrink;
    case kSbBorderW: return &s.borderW;
    case kSbRadius: return &s.radius;
    case kSbAlpha: return &s.alpha;
    case kSbShadow: return &s.shadow;
    case kSbIconSize: return &s.iconSize;
    case kSbHoverAlpha: return &s.hoverAlpha;
    case kSbBasis: return &s.basis;
    default: return nullptr;
    }
}

template <class S>
auto bslot(S& s, uint32_t bit) -> decltype(&s.flexDir) {
    switch (bit) {
    case kSbFlexDir: return &s.flexDir;
    case kSbAlignItems: return &s.alignItems;
    case kSbAlignSelf: return &s.alignSelf;
    case kSbJustify: return &s.justify;
    case kSbWrap: return &s.wrap;
    case kSbOverflow: return &s.overflow;
    case kSbPosition: return &s.position;
    case kSbToken: return &s.token;
    case kSbBorderToken: return &s.borderToken;
    case kSbActiveToken: return &s.activeToken;
    case kSbTextSize: return &s.textSize;
    case kSbTextAlign: return &s.textAlign;
    default: return nullptr;
    }
}

// Copies every field src sets onto dst. A new width/height replaces the old one's unit (px, %, auto) as a whole.
void overlay(GuiStyle& dst, const GuiStyle& src) {
    const uint64_t wMask = mask(kSbW) | mask(kSbWPercent) | mask(kSbWAuto);
    const uint64_t hMask = mask(kSbH) | mask(kSbHPercent) | mask(kSbHAuto);
    if ((src.set & wMask) != 0) {
        dst.set &= ~wMask;
    }
    if ((src.set & hMask) != 0) {
        dst.set &= ~hMask;
    }
    for (uint32_t bit = 0; bit < kSbCount; ++bit) {
        if ((src.set >> bit & 1u) == 0) {
            continue;
        }
        if (float* d = fslot(dst, bit)) {
            *d = *fslot(src, bit);
        } else if (uint8_t* b = bslot(dst, bit)) {
            *b = *bslot(src, bit);
        }
    }
    dst.set |= src.set;
}

struct NumKey {
    const char* key;
    uint8_t n;
    GuiStyleBit bits[4];
};

constexpr NumKey kNumKeys[] = {
    {"w", 1, {kSbW}}, {"h", 1, {kSbH}}, {"minW", 1, {kSbMinW}}, {"minH", 1, {kSbMinH}}, {"maxW", 1, {kSbMaxW}}, {"maxH", 1, {kSbMaxH}},
    {"pad", 4, {kSbPadL, kSbPadT, kSbPadR, kSbPadB}}, {"padH", 2, {kSbPadL, kSbPadR}}, {"padV", 2, {kSbPadT, kSbPadB}},
    {"padL", 1, {kSbPadL}}, {"padT", 1, {kSbPadT}}, {"padR", 1, {kSbPadR}}, {"padB", 1, {kSbPadB}},
    {"margin", 4, {kSbMarL, kSbMarT, kSbMarR, kSbMarB}}, {"marginL", 1, {kSbMarL}}, {"marginT", 1, {kSbMarT}},
    {"marginR", 1, {kSbMarR}}, {"marginB", 1, {kSbMarB}},
    {"gap", 1, {kSbGap}},
    {"left", 1, {kSbPosL}}, {"top", 1, {kSbPosT}}, {"right", 1, {kSbPosR}}, {"bottom", 1, {kSbPosB}},
    {"grow", 1, {kSbGrow}}, {"flexGrow", 1, {kSbGrow}}, {"shrink", 1, {kSbShrink}}, {"flexShrink", 1, {kSbShrink}},
    {"border", 1, {kSbBorderW}}, {"borderW", 1, {kSbBorderW}}, {"radius", 1, {kSbRadius}}, {"alpha", 1, {kSbAlpha}},
    {"shadow", 1, {kSbShadow}}, {"icon", 1, {kSbIconSize}}, {"iconSize", 1, {kSbIconSize}},
    {"textSize", 1, {kSbTextSize}}, {"fontSize", 1, {kSbTextSize}}, {"hoverAlpha", 1, {kSbHoverAlpha}},
    {"basis", 1, {kSbBasis}}, {"flexBasis", 1, {kSbBasis}},
};

struct EnumName {
    const char* name;
    uint8_t value;
};

constexpr EnumName kDirNames[] = {{"column", 0}, {"row", 1}, {"column-reverse", 2}, {"row-reverse", 3}};
constexpr EnumName kAlignNames[] = {{"auto", 0}, {"start", 1}, {"flex-start", 1}, {"center", 2}, {"end", 3}, {"flex-end", 3}, {"stretch", 4}, {"baseline", 5}};
constexpr EnumName kJustifyNames[] = {{"start", 0}, {"flex-start", 0}, {"center", 1}, {"end", 2}, {"flex-end", 2},
                                       {"space-between", 3}, {"space-around", 4}, {"space-evenly", 5}};
constexpr EnumName kWrapNames[] = {{"nowrap", 0}, {"wrap", 1}, {"wrap-reverse", 2}};
constexpr EnumName kOverflowNames[] = {{"visible", 0}, {"hidden", 1}, {"scroll", 2}};
constexpr EnumName kPositionNames[] = {{"relative", 0}, {"absolute", 1}};
constexpr EnumName kTextAlignNames[] = {{"left", 0}, {"center", 1}, {"right", 2}};

struct EnumKey {
    const char* key;
    GuiStyleBit bit;
    std::span<const EnumName> names;
};

constexpr EnumKey kEnumKeys[] = {
    {"flexDir", kSbFlexDir, kDirNames}, {"direction", kSbFlexDir, kDirNames},
    {"align", kSbAlignItems, kAlignNames}, {"alignItems", kSbAlignItems, kAlignNames}, {"alignSelf", kSbAlignSelf, kAlignNames},
    {"justify", kSbJustify, kJustifyNames}, {"wrap", kSbWrap, kWrapNames}, {"overflow", kSbOverflow, kOverflowNames},
    {"position", kSbPosition, kPositionNames}, {"textAlign", kSbTextAlign, kTextAlignNames},
};

struct TokenKey {
    const char* key;
    GuiStyleBit bit;
};

constexpr TokenKey kTokenKeys[] = {{"bg", kSbToken}, {"activeBg", kSbActiveToken}, {"borderBg", kSbBorderToken}};

// ---------------------------------------------------------------- "tokens" constants

struct Consts {
    char name[kGuiStyleTokenCap][24] = {};
    float value[kGuiStyleTokenCap] = {};
    uint32_t count = 0;
};

bool constFind(const Consts& table, const char* name, float& out) {
    for (uint32_t i = 0; i < table.count; ++i) {
        if (std::strcmp(table.name[i], name) == 0) {
            out = table.value[i];
            return true;
        }
    }
    return false;
}

bool constAdd(Consts& table, const char* group, const char* key, float value) {
    char full[48];
    const int n = group != nullptr ? std::snprintf(full, sizeof(full), "%s.%s", group, key) : std::snprintf(full, sizeof(full), "%s", key);
    if (n <= 0 || n >= static_cast<int>(sizeof(table.name[0])) || table.count >= kGuiStyleTokenCap) {
        return false;
    }
    std::memcpy(table.name[table.count], full, static_cast<size_t>(n) + 1u);
    table.value[table.count++] = value;
    return true;
}

void skipComma(Cur& c) {
    json::skip(c);
    if (c.p < c.end && *c.p == ',') {
        ++c.p;
        json::skip(c);
    }
}

// {"group": {"name": 1}, "name": 2} -> "group.name", "name".
bool parseTokens(Cur& c, Consts& out) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '{') {
            ++c.p;
            json::skip(c);
            while (c.p < c.end && *c.p != '}') {
                char inner[24]{};
                double number = 0.0;
                if (!json::parseStringCap(c, inner, sizeof(inner), n) || !json::eat(c, ':') || !json::parseNumber(c, number) ||
                    !constAdd(out, key, inner, static_cast<float>(number))) {
                    return false;
                }
                skipComma(c);
            }
            if (!json::eat(c, '}')) {
                return false;
            }
        } else {
            double number = 0.0;
            if (!json::parseNumber(c, number) || !constAdd(out, nullptr, key, static_cast<float>(number))) {
                return false;
            }
        }
        skipComma(c);
    }
    return json::eat(c, '}');
}

// ---------------------------------------------------------------- values

enum class Unit : uint8_t { Px, Percent, Auto };

// A number, "$name" / "$group.name", "$theme.radius", "$theme.radiusSm", "auto", or "NN%".
bool parseValue(Cur& c, const Consts& tokens, float& out, Unit& unit) {
    unit = Unit::Px;
    json::skip(c);
    if (c.p < c.end && *c.p == '"') {
        char text[32]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, text, sizeof(text), n)) {
            return false;
        }
        const std::string_view ref{text, n};
        if (ref == "$theme.radius") {
            out = kGuiRadiusTheme;
            return true;
        }
        if (ref == "$theme.radiusSm") {
            out = kGuiRadiusThemeSm;
            return true;
        }
        if (ref == "auto") {
            unit = Unit::Auto;
            out = 0.0f;
            return true;
        }
        if (n > 1 && ref.back() == '%') {
            const auto [ptr, ec] = std::from_chars(text, text + n - 1, out);
            unit = Unit::Percent;
            return ec == std::errc() && ptr == text + n - 1;
        }
        return n > 1 && text[0] == '$' && constFind(tokens, text + 1, out);
    }
    double number = 0.0;
    if (!json::parseNumber(c, number)) {
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

// ---------------------------------------------------------------- sheet storage

uint32_t nameHash(std::string_view name) {
    uint32_t h = 2166136261u;
    for (const char ch : name) {
        h = (h ^ static_cast<uint8_t>(ch)) * 16777619u;
    }
    return h;
}

uint32_t findIndex(const GuiStyleSheet& sheet, std::string_view name) {
    const uint32_t hash = nameHash(name);
    for (uint32_t i = 0; i < sheet.count; ++i) {
        if (sheet.hashes[i] == hash && name == sheet.names[i]) {
            return i;
        }
    }
    return kGuiNoClass;
}

// Parses one class object into `out` (extends folded in).
bool parseClass(Cur& c, const Consts& tokens, const GuiStyleSheet& sheet, GuiStyle& out) {
    if (!json::eat(c, '{')) {
        return false;
    }
    GuiStyle own{};
    char base[kGuiStyleNameCap]{};
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[32]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        const std::string_view k{key, n};
        bool handled = false;
        for (const NumKey& entry : kNumKeys) {
            if (k != entry.key) {
                continue;
            }
            float value = 0.0f;
            Unit unit = Unit::Px;
            if (!parseValue(c, tokens, value, unit)) {
                return false;
            }
            const bool isW = entry.bits[0] == kSbW;
            const bool isH = entry.bits[0] == kSbH;
            if (unit != Unit::Px && !isW && !isH) {
                return false;
            }
            for (uint8_t i = 0; i < entry.n; ++i) {
                const GuiStyleBit bit = entry.bits[i];
                if (unit == Unit::Auto) {
                    own.set = (own.set & ~(mask(bit) | mask(isW ? kSbWPercent : kSbHPercent))) | mask(isW ? kSbWAuto : kSbHAuto);
                    continue;
                }
                if (float* f = fslot(own, bit)) {
                    *f = value;
                } else if (uint8_t* b = bslot(own, bit)) { // byte-sized numeric fields (textSize)
                    *b = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
                }
                own.set |= mask(bit);
                if (isW || isH) {
                    const uint64_t pct = mask(isW ? kSbWPercent : kSbHPercent);
                    const uint64_t autoBit = mask(isW ? kSbWAuto : kSbHAuto);
                    own.set = (own.set & ~(pct | autoBit)) | (unit == Unit::Percent ? pct : 0u);
                }
            }
            handled = true;
            break;
        }
        for (const TokenKey& entry : kTokenKeys) {
            if (handled || k != entry.key) {
                continue;
            }
            char text[24]{};
            uint8_t id = 0;
            if (!json::parseStringCap(c, text, sizeof(text), n) || !tokenNamed(std::string_view{text, n}, id)) {
                return false;
            }
            own.set |= mask(entry.bit);
            *bslot(own, entry.bit) = id;
            handled = true;
        }
        for (const EnumKey& entry : kEnumKeys) {
            if (handled || k != entry.key) {
                continue;
            }
            char text[24]{};
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            bool found = false;
            for (const EnumName& name : entry.names) {
                if (std::string_view{text, n} == name.name) {
                    *bslot(own, entry.bit) = name.value;
                    own.set |= mask(entry.bit);
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
            handled = true;
        }
        if (!handled && k == "extends") {
            if (!json::parseStringCap(c, base, sizeof(base), n)) {
                return false;
            }
            handled = true;
        }
        if (!handled && !json::skipValue(c)) {
            return false; // unknown legacy keys (inner, chrome, ...) are skipped
        }
        skipComma(c);
    }
    if (!json::eat(c, '}')) {
        return false;
    }
    GuiStyle result{};
    if (base[0] != '\0') {
        const uint32_t parent = findIndex(sheet, base);
        if (parent == kGuiNoClass) {
            return false; // a class extends only classes defined before it
        }
        result = sheet.cls[parent];
    }
    overlay(result, own);
    out = result;
    return true;
}

bool parseClasses(Cur& c, const Consts& tokens, GuiStyleSheet& sheet, bool overlayMode) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char name[kGuiStyleNameCap]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, name, sizeof(name), n) || !json::eat(c, ':')) {
            return false;
        }
        const uint32_t existing = findIndex(sheet, std::string_view{name, n});
        if (existing != kGuiNoClass && !overlayMode) {
            return false; // duplicate inside one document
        }
        if (existing == kGuiNoClass && sheet.count >= kGuiStyleCap) {
            return false;
        }
        GuiStyle style{};
        if (!parseClass(c, tokens, sheet, style)) {
            return false;
        }
        if (existing != kGuiNoClass) {
            overlay(sheet.cls[existing], style);
        } else {
            sheet.cls[sheet.count] = style;
            std::memcpy(sheet.names[sheet.count], name, sizeof(name));
            sheet.hashes[sheet.count] = nameHash(std::string_view{name, n});
            ++sheet.count;
        }
        skipComma(c);
    }
    return json::eat(c, '}');
}

enum class Seek : uint8_t { Found, Absent, Malformed };

// Walks the top-level object once for `wanted`, leaving the cursor on its value.
Seek seek(Cur& c, std::string_view wanted) {
    if (!json::eat(c, '{')) {
        return Seek::Malformed;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return Seek::Malformed;
        }
        if (std::string_view{key, n} == wanted) {
            return Seek::Found;
        }
        if (!json::skipValue(c)) {
            return Seek::Malformed;
        }
        skipComma(c);
    }
    return Seek::Absent;
}

bool loadImpl(GuiStyleSheet& sheet, std::string_view text, bool overlayMode) {
    if (sheet.ready == 0) {
        guiStyleInit(sheet);
    }
    if (!overlayMode) {
        sheet.count = 0;
        sheet.constCount = 0;
    }
    Consts tokens;
    const Cur whole{text.data(), text.data() + text.size()};
    Cur scan = whole;
    const Seek hasTokens = seek(scan, "tokens");
    bool ok = hasTokens != Seek::Malformed && (hasTokens == Seek::Absent || parseTokens(scan, tokens));
    if (ok) {
        for (uint32_t i = 0; i < tokens.count; ++i) {
            uint32_t slot = 0;
            while (slot < sheet.constCount && std::strcmp(sheet.constName[slot], tokens.name[i]) != 0) {
                ++slot;
            }
            if (slot < kGuiStyleTokenCap) {
                std::memcpy(sheet.constName[slot], tokens.name[i], sizeof(sheet.constName[slot]));
                sheet.constValue[slot] = tokens.value[i];
                sheet.constCount = std::max(sheet.constCount, slot + 1u);
            }
        }
        scan = whole;
        const Seek hasClasses = seek(scan, "classes");
        ok = hasClasses == Seek::Absent || (hasClasses == Seek::Found && parseClasses(scan, tokens, sheet, overlayMode));
    }
    if (!ok && !overlayMode) {
        sheet.count = 0;
    }
    return ok;
}

// ---------------------------------------------------------------- theme

struct ThemeColor {
    const char* key;
    uint8_t id;
};

constexpr ThemeColor kThemeColors[] = {
    {"window", kTokWindow}, {"title", kTokTitle}, {"side", kTokSide}, {"content", kTokContent}, {"button", kTokButton},
    {"field", kTokField}, {"menu", kTokMenu}, {"close", kTokClose}, {"text", kTokText}, {"muted", kTokMuted},
    {"item", kTokItem}, {"preview", kTokPreview}, {"status", kTokStatus}, {"slider", kTokSlider}, {"check", kTokCheck},
    {"chrome", kTokChrome}, {"tool", kTokTool}, {"toolHover", kTokToolHover}, {"toolActive", kTokToolActive},
    {"accent0", kTokAccent}, {"accent1", kTokAccent1}, {"accent2", kTokAccent2},
};

bool parseTriple(Cur& c, float (&out)[4]) {
    if (!json::eat(c, '[')) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        double v = 0.0;
        if (!json::parseNumber(c, v)) {
            return false;
        }
        out[i] = static_cast<float>(v);
        if (i < 2 && !json::eat(c, ',')) {
            return false;
        }
    }
    return json::eat(c, ']');
}

bool parseThemeBody(Cur& c, GuiStyleSheet& sheet) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        const std::string_view k{key, n};
        bool handled = false;
        for (const ThemeColor& entry : kThemeColors) {
            if (k == entry.key) {
                if (!parseTriple(c, sheet.colors[entry.id])) {
                    return false;
                }
                handled = true;
                break;
            }
        }
        float* scalar = k == "radius" ? &sheet.themeRadius : k == "radiusSm" ? &sheet.themeRadiusSm : k == "hover" ? &sheet.hover : nullptr;
        if (!handled && scalar != nullptr) {
            double v = 0.0;
            if (!json::parseNumber(c, v)) {
                return false;
            }
            *scalar = static_cast<float>(v);
            handled = true;
        }
        if (!handled && !json::skipValue(c)) {
            return false;
        }
        skipComma(c);
    }
    return json::eat(c, '}');
}

} // namespace

void guiStyleInit(GuiStyleSheet& sheet) {
    sheet.count = 0;
    resetLook(sheet);
    guiTextMetricsDefault(sheet.text);
    sheet.ready = 1;
}

bool guiStyleLoad(GuiStyleSheet& sheet, std::string_view json) {
    return loadImpl(sheet, json, false);
}

bool guiStyleLoadOver(GuiStyleSheet& sheet, std::string_view json) {
    return loadImpl(sheet, json, true);
}

bool guiStyleLoadFile(GuiStyleSheet& sheet, const char* path) {
    MappedFile file{};
    if (path == nullptr || !fileMapReadOnly(path, &file) || file.data == nullptr) {
        return false;
    }
    const bool ok = guiStyleLoad(sheet, std::string_view{reinterpret_cast<const char*>(file.data), file.size});
    fileUnmap(&file);
    return ok;
}

bool guiStyleLoadTheme(GuiStyleSheet& sheet, std::string_view text, std::string_view theme) {
    if (sheet.ready == 0) {
        guiStyleInit(sheet);
    }
    Cur c{text.data(), text.data() + text.size()};
    if (seek(c, "themes") != Seek::Found || !json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char name[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, name, sizeof(name), n) || !json::eat(c, ':')) {
            return false;
        }
        if (std::string_view{name, n} == theme) {
            resetLook(sheet);
            const bool ok = parseThemeBody(c, sheet);
            std::memcpy(sheet.colors[kTokScroll], sheet.colors[kTokContent], sizeof(sheet.colors[0]));
            return ok;
        }
        if (!json::skipValue(c)) {
            return false;
        }
        skipComma(c);
    }
    return false;
}

bool guiStyleColor8(const GuiStyleSheet& sheet, std::string_view role, uint8_t (&rgb)[3]) {
    uint8_t id = 0;
    if (!tokenNamed(role, id) || id >= kGuiPaletteCap) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        rgb[i] = static_cast<uint8_t>(std::clamp(sheet.colors[id][i], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return true;
}

bool guiStyleNumber(const GuiStyleSheet& sheet, std::string_view name, float& out) {
    for (uint32_t i = 0; i < sheet.constCount; ++i) {
        if (name == sheet.constName[i]) {
            out = sheet.constValue[i];
            return true;
        }
    }
    return false;
}

uint32_t guiStyleFind(const GuiStyleSheet& sheet, std::string_view name) {
    return findIndex(sheet, name);
}

const GuiStyle* guiStyleGet(const GuiStyleSheet& sheet, uint32_t classId) {
    return classId < sheet.count ? &sheet.cls[classId] : nullptr;
}

float guiStyleRadius(const GuiStyleSheet& sheet, const GuiStyle& style) {
    if (style.radius == kGuiRadiusTheme) {
        return sheet.themeRadius;
    }
    if (style.radius == kGuiRadiusThemeSm) {
        return sheet.themeRadiusSm;
    }
    return style.radius;
}

float guiStyleHoverAlpha(const GuiStyleSheet& sheet, uint32_t classId) {
    const GuiStyle* style = guiStyleGet(sheet, classId);
    if (style == nullptr) {
        return 0.0f;
    }
    return guiStyleHas(*style, kSbHoverAlpha) ? style->hoverAlpha : sheet.hover;
}

void guiStyleColor(const GuiStyleSheet& sheet, uint8_t token, float rgba[4]) {
    BH_ASSERT(rgba != nullptr, "guiStyleColor needs an output");
    const uint8_t id = token < kTokCount ? token : static_cast<uint8_t>(kTokCustom);
    rgba[0] = sheet.colors[id][0];
    rgba[1] = sheet.colors[id][1];
    rgba[2] = sheet.colors[id][2];
    rgba[3] = 1.0f;
}

} // namespace burnhope::gui
