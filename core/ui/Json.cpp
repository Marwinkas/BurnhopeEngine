#include "Str.hpp"
#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/FlexAttr.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace burnhope {
namespace {

using json::Cur;
using json::eat;
using json::parseNumber;
using json::parseStringCap;
using json::skip;
using json::skipValue;

bool parseUiNumber(Cur& c, float& out) {
    double v = 0.0;
    if (!parseNumber(c, v)) {
        return false;
    }
    out = static_cast<float>(v);
    return true;
}

bool parseUiString(Cur& c, char* dst, uint8_t cap, uint8_t& len) {
    uint32_t n = 0;
    if (!parseStringCap(c, dst, cap, n) || n > 255) {
        return false;
    }
    len = static_cast<uint8_t>(n);
    return true;
}

bool tokenNamed(const char* word, UiToken& out) {
    struct Pair {
        const char* name;
        UiToken token;
    };
    const Pair table[] = {
        {"Custom", UiToken::Custom},
        {"Window", UiToken::Window},
        {"Title", UiToken::Title},
        {"Side", UiToken::Side},
        {"Content", UiToken::Content},
        {"Button", UiToken::Button},
        {"Field", UiToken::Field},
        {"Menu", UiToken::Menu},
        {"Close", UiToken::Close},
        {"Text", UiToken::Text},
        {"Muted", UiToken::Muted},
        {"Item", UiToken::Item},
        {"Preview", UiToken::Preview},
        {"Status", UiToken::Status},
        {"Slider", UiToken::Slider},
        {"Check", UiToken::Check},
        {"Scroll", UiToken::Scroll},
        {"Chrome", UiToken::Chrome},
        {"Accent", UiToken::Accent},
    };
    for (const Pair& row : table) {
        if (strSame(word, row.name)) {
            out = row.token;
            return true;
        }
    }
    return false;
}

bool parseSizeFloat(const char* begin, const char* end, float& out) {
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr != begin;
}

void readSize(Cur& c, UiSize& mode, float& value) {
    skip(c);
    if (c.p < c.end && *c.p == '"') {
        char buf[16]{};
        uint8_t n = 0;
        if (!parseUiString(c, buf, 16, n)) {
            return;
        }
        if (n > 0 && buf[n - 1] == '%') {
            mode = UiSize::Percent;
            (void)parseSizeFloat(buf, buf + n - 1, value);
        } else {
            mode = UiSize::Px;
            (void)parseSizeFloat(buf, buf + n, value);
        }
        return;
    }
    float v = 0.0f;
    if (parseUiNumber(c, v)) {
        mode = UiSize::Px;
        value = v;
    }
}

void flexToBin(BinNode& bin, const UiFlex& flex) noexcept {
    bin.direction = static_cast<uint8_t>(flex.direction);
    bin.wrap = static_cast<uint8_t>(flex.wrap);
    bin.justify = static_cast<uint8_t>(flex.justify);
    bin.align = static_cast<uint8_t>(flex.align);
    bin.widthMode = static_cast<uint8_t>(flex.widthMode);
    bin.heightMode = static_cast<uint8_t>(flex.heightMode);
    bin.position = static_cast<uint8_t>(flex.position);
    bin.width = flex.width;
    bin.height = flex.height;
    bin.grow = flex.grow;
    bin.shrink = flex.shrink;
    bin.pad = flex.pad;
    bin.padL = flex.padL;
    bin.padR = flex.padR;
    bin.padT = flex.padT;
    bin.padB = flex.padB;
    bin.gap = flex.gap;
    bin.posX = flex.posX;
    bin.posY = flex.posY;
    bin.minW = flex.minW;
    bin.minH = flex.minH;
    bin.marginL = flex.marginL;
    bin.marginR = flex.marginR;
    bin.marginT = flex.marginT;
    bin.marginB = flex.marginB;
    bin.drop = static_cast<uint8_t>(flex.drop);
    bin.content = static_cast<uint8_t>(flex.content);
    bin.self = static_cast<uint8_t>(flex.self);
    bin.basisMode = static_cast<uint8_t>(flex.basisMode);
    bin.maxWMode = static_cast<uint8_t>(flex.maxWMode);
    bin.maxHMode = static_cast<uint8_t>(flex.maxHMode);
    bin.overflow = static_cast<uint8_t>(flex.overflow);
    bin.box = static_cast<uint8_t>(flex.box);
    bin.inset = static_cast<uint8_t>(flex.inset);
    bin.basis = flex.basis;
    bin.maxW = flex.maxW;
    bin.maxH = flex.maxH;
    bin.gapRow = flex.gapRow;
    bin.aspect = flex.aspect;
    bin.posR = flex.posR;
    bin.posB = flex.posB;
}

uint16_t parseNode(UiState& s, uint16_t parent, Cur& c) {
    if (!eat(c, '{')) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.shrink = 0.0f;
    BinNode bin{};
    bin.a = 1.0f;
    bin.shown = 1;
    char name[32]{};
    char text[96]{};
    char hint[40]{};
    uint8_t textLen = 0;
    uint8_t hintLen = 0;
    BinTail role{};
    const char* kids = nullptr;
    bool filled = false;
    UiToken ink = UiToken::Content;
    skip(c);
    if (!eat(c, '}')) {
        for (;;) {
            char key[32]{};
            uint8_t kl = 0;
            if (!parseUiString(c, key, 32, kl) || !eat(c, ':')) {
                break;
            }
            if (strSame(key, "id")) {
                uint8_t n = 0;
                parseUiString(c, name, 32, n);
            } else if (strSame(key, "text")) {
                parseUiString(c, text, 96, textLen);
            } else if (strSame(key, "role")) {
                char role[16]{};
                uint8_t n = 0;
                if (parseUiString(c, role, 16, n)) {
                    const uint8_t found = uiBinRoleFind(s, role);
                    if (found != 0) {
                        bin.role = found;
                        if (s.binRole[found].token != 0) {
                            ink = static_cast<UiToken>(s.binRole[found].token);
                        }
                    }
                }
            } else if (strSame(key, "dir") || strSame(key, "wrap") || strSame(key, "justify") || strSame(key, "align") || strSame(key, "content") || strSame(key, "self") || strSame(key, "overflow") || strSame(key, "box") || strSame(key, "position")) {
                char word[24]{};
                uint8_t n = 0;
                if (parseUiString(c, word, 24, n)) {
                    uiFlexApplyAttr(flex, key, word);
                }
            } else if (strSame(key, "fill") && !eat(c, '[')) {
                char word[24]{};
                uint8_t n = 0;
                UiToken named = UiToken::Content;
                if (parseUiString(c, word, 24, n) && tokenNamed(word, named)) {
                    ink = named;
                    filled = false;
                }
            } else if (strSame(key, "fill")) {
                float chans[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                int n = 0;
                for (;;) {
                    float v = 0.0f;
                    if (n < 4 && parseUiNumber(c, v)) {
                        chans[n++] = v;
                    }
                    if (eat(c, ']')) {
                        break;
                    }
                    if (!eat(c, ',')) {
                        break;
                    }
                }
                if (n >= 3) {
                    bin.r = chans[0];
                    bin.g = chans[1];
                    bin.b = chans[2];
                    if (n >= 4) {
                        bin.a = chans[3];
                    }
                    ink = UiToken::Custom;
                    filled = true;
                }
            } else if (strSame(key, "w")) {
                UiSize mode = UiSize::Auto;
                float value = 0.0f;
                readSize(c, mode, value);
                uiFlexApplyAttr(flex, "w", static_cast<uint8_t>(mode), value);
            } else if (strSame(key, "h")) {
                UiSize mode = UiSize::Auto;
                float value = 0.0f;
                readSize(c, mode, value);
                uiFlexApplyAttr(flex, "h", static_cast<uint8_t>(mode), value);
            } else if (strSame(key, "basis") || strSame(key, "maxW") || strSame(key, "maxH")) {
                UiSize mode = UiSize::Auto;
                float value = 0.0f;
                readSize(c, mode, value);
                uiFlexApplyAttr(flex, key, static_cast<uint8_t>(mode), value);
            } else if (strSame(key, "gapRow") || strSame(key, "aspect") || strSame(key, "right") || strSame(key, "bottom") || strSame(key, "grow") || strSame(key, "shrink") || strSame(key, "padL") || strSame(key, "padR")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    uiFlexApplyAttr(flex, key, v);
                }
            } else if (strSame(key, "pad") || strSame(key, "gap") || strSame(key, "radius") || strSame(key, "icon")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    if (strSame(key, "pad") || strSame(key, "gap")) {
                        uiFlexApplyAttr(flex, key, v);
                    } else if (strSame(key, "radius")) {
                        bin.radius = v;
                    } else {
                        bin.icon = static_cast<uint8_t>(v);
                    }
                }
            } else if (strSame(key, "hint")) {
                parseUiString(c, hint, 40, hintLen);
            } else if (strSame(key, "filter") || strSame(key, "kind") || strSame(key, "group") || strSame(key, "max") || strSame(key, "textWrap") || strSame(key, "ellipsis") || strSame(key, "deco") || strSame(key, "clear") || strSame(key, "required") || strSame(key, "readonly") || strSame(key, "password") || strSame(key, "form") || strSame(key, "on")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    const auto n = static_cast<uint8_t>(v);
                    if (strSame(key, "filter")) {
                        role.raw[0] = n;
                    } else if (strSame(key, "kind")) {
                        role.raw[2] = n;
                    } else if (strSame(key, "group")) {
                        role.raw[3] = n;
                    } else if (strSame(key, "max")) {
                        role.raw[4] = n;
                    } else if (strSame(key, "textWrap")) {
                        role.raw[5] = n;
                    } else if (strSame(key, "ellipsis")) {
                        role.raw[7] = n;
                    } else if (strSame(key, "deco")) {
                        role.raw[9] = n;
                    } else if (n != 0) {
                        uint8_t bit = 0;
                        if (strSame(key, "clear")) {
                            bit = 1u << 0;
                        } else if (strSame(key, "required")) {
                            bit = 1u << 1;
                        } else if (strSame(key, "readonly")) {
                            bit = 1u << 2;
                        } else if (strSame(key, "password")) {
                            bit = 1u << 3;
                        } else if (strSame(key, "form")) {
                            bit = 1u << 4;
                        } else if (strSame(key, "on")) {
                            bit = 1u << 6;
                        }
                        role.raw[1] = static_cast<uint8_t>(role.raw[1] | bit);
                    }
                }
            } else if (strSame(key, "min") || strSame(key, "maxv") || strSame(key, "value") || strSame(key, "leading")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    uint8_t at = 10;
                    if (strSame(key, "maxv")) {
                        at = 14;
                    } else if (strSame(key, "value")) {
                        at = 18;
                    } else if (strSame(key, "leading")) {
                        at = 22;
                    }
                    std::memcpy(role.raw + at, &v, sizeof(v));
                }
            } else if (strSame(key, "children") && eat(c, '[')) {
                kids = c.p;
                if (!eat(c, ']')) {
                    for (;;) {
                        skipValue(c);
                        if (eat(c, ']')) {
                            break;
                        }
                        if (!eat(c, ',')) {
                            break;
                        }
                    }
                }
            } else {
                skipValue(c);
            }
            if (eat(c, '}')) {
                break;
            }
            if (!eat(c, ',')) {
                break;
            }
        }
    }
    if (!filled) {
        tokenRgb(s.look, ink, bin.r, bin.g, bin.b);
    }
    bin.token = static_cast<uint8_t>(ink);
    flexToBin(bin, flex);
    const uint16_t id = uiSpawnDescribed(s, parent, bin, role, name[0] != '\0' ? name : nullptr, textLen > 0 ? text : nullptr, hintLen > 0 ? hint : nullptr);
    if (id != kUiNone && kids != nullptr) {
        Cur nested{kids, c.end};
        if (!eat(nested, ']')) {
            for (;;) {
                parseNode(s, id, nested);
                if (eat(nested, ']')) {
                    break;
                }
                if (!eat(nested, ',')) {
                    break;
                }
            }
        }
    }
    return id;
}

} // namespace

uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text) {
    if (text == nullptr) {
        return kUiNone;
    }
    Cur cur{text, text + std::strlen(text)};
    return parseNode(s, parent, cur);
}

uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path) {
    if (path == nullptr) {
        spdlog::error("ui: failed to read json file");
        return kUiNone;
    }
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("ui: failed to read json file {}", path);
        return kUiNone;
    }
    if (mapped.size == 0 || mapped.size > 8u * 1024u * 1024u || mapped.data == nullptr) {
        fileUnmap(&mapped);
        spdlog::error("ui: failed to read json file {}", path);
        return kUiNone;
    }
    const char* begin = reinterpret_cast<const char*>(mapped.data);
    const char* end = begin + mapped.size;
    Cur cur{begin, end};
    const uint16_t root = parseNode(s, parent, cur);
    fileUnmap(&mapped);
    return root;
}

} // namespace burnhope

