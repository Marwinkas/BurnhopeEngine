#include "Str.hpp"
#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/FlexAttr.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace burnhope {
namespace {

using json::Cur;
using json::eat;
using json::parseNumber;
using json::parseStringCap;
using json::skip;

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

void skipValue(Cur& c) {
    skip(c);
    const char* before = c.p;
    if (c.p >= c.end) {
        return;
    }
    if (*c.p == '"') {
        char tmp[4]{};
        uint8_t n = 0;
        parseUiString(c, tmp, 4, n);
        return;
    }
    if (*c.p == '{') {
        eat(c, '{');
        if (eat(c, '}')) {
            return;
        }
        for (;;) {
            char key[32]{};
            uint8_t kl = 0;
            if (!parseUiString(c, key, 32, kl) || !eat(c, ':')) {
                return;
            }
            skipValue(c);
            if (eat(c, '}')) {
                return;
            }
            if (!eat(c, ',')) {
                return;
            }
        }
    }
    if (*c.p == '[') {
        eat(c, '[');
        if (eat(c, ']')) {
            return;
        }
        for (;;) {
            skipValue(c);
            if (eat(c, ']')) {
                return;
            }
            if (!eat(c, ',')) {
                return;
            }
        }
    }
    if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        return;
    }
    if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        return;
    }
    if (c.p + 4 <= c.end && std::memcmp(c.p, "null", 4) == 0) {
        c.p += 4;
        return;
    }
    float dummy = 0.0f;
    if (!parseUiNumber(c, dummy) && c.p == before && c.p < c.end) {
        ++c.p;
    }
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

uint16_t parseNode(UiState& s, uint16_t parent, Cur& c) {
    if (!eat(c, '{')) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.shrink = 0.0f;
    UiPaint paint = paintToken(s.look, UiToken::Content, 0.0f);
    paint.a = 1.0f;
    const uint16_t id = uiNode(s, parent, flex, paint);
    if (id == kUiNone) {
        skipValue(c);
        return kUiNone;
    }
    char text[96]{};
    uint8_t textLen = 0;
    UiSize wMode = UiSize::Auto;
    UiSize hMode = UiSize::Auto;
    float width = 0.0f;
    float height = 0.0f;
    bool sawW = false;
    bool sawH = false;
    skip(c);
    if (!eat(c, '}')) {
        for (;;) {
            char key[32]{};
            uint8_t kl = 0;
            if (!parseUiString(c, key, 32, kl) || !eat(c, ':')) {
                break;
            }
            if (strSame(key, "id")) {
                char name[32]{};
                uint8_t n = 0;
                if (parseUiString(c, name, 32, n)) {
                    uiName(s, id, name);
                }
            } else if (strSame(key, "text")) {
                parseUiString(c, text, 96, textLen);
            } else if (strSame(key, "role")) {
                char role[16]{};
                uint8_t n = 0;
                if (parseUiString(c, role, 16, n)) {
                    auto* node = s.ent[id].try_get_mut<UiPaint>();
                    if (node != nullptr) {
                        if (strSame(role, "button")) {
                            *node = paintToken(s.look, UiToken::Button, 0.0f);
                        } else if (strSame(role, "label")) {
                            *node = paintLabel(s.look);
                        } else if (strSame(role, "field")) {
                            *node = paintToken(s.look, UiToken::Field, 0.0f);
                        } else if (strSame(role, "check")) {
                            *node = paintToken(s.look, UiToken::Check, 0.0f);
                        } else {
                            *node = paintToken(s.look, UiToken::Content, 0.0f);
                        }
                    }
                }
            } else if (strSame(key, "dir") || strSame(key, "wrap") || strSame(key, "justify") || strSame(key, "align") || strSame(key, "content") || strSame(key, "self") || strSame(key, "overflow") || strSame(key, "box") || strSame(key, "position")) {
                char word[24]{};
                uint8_t n = 0;
                if (parseUiString(c, word, 24, n)) {
                    if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                        uiFlexApplyAttr(*node, key, word);
                    }
                }
            } else if (strSame(key, "fill") && eat(c, '[')) {
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
                auto* node = s.ent[id].try_get_mut<UiPaint>();
                if (node != nullptr && n >= 3) {
                    node->r = chans[0];
                    node->g = chans[1];
                    node->b = chans[2];
                    if (n >= 4) {
                        node->a = chans[3];
                    }
                }
            } else if (strSame(key, "w")) {
                readSize(c, wMode, width);
                sawW = true;
            } else if (strSame(key, "h")) {
                readSize(c, hMode, height);
                sawH = true;
            } else if (strSame(key, "basis") || strSame(key, "maxW") || strSame(key, "maxH")) {
                UiSize mode = UiSize::Auto;
                float value = 0.0f;
                readSize(c, mode, value);
                if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                    uiFlexApplyAttr(*node, key, static_cast<uint8_t>(mode), value);
                }
            } else if (strSame(key, "gapRow") || strSame(key, "aspect") || strSame(key, "right") || strSame(key, "bottom") || strSame(key, "grow")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                        uiFlexApplyAttr(*node, key, v);
                    }
                }
            } else if (strSame(key, "pad") || strSame(key, "gap") || strSame(key, "radius") || strSame(key, "icon")) {
                float v = 0.0f;
                if (parseUiNumber(c, v)) {
                    if (strSame(key, "pad") || strSame(key, "gap")) {
                        if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                            uiFlexApplyAttr(*node, key, v);
                        }
                    } else if (strSame(key, "radius")) {
                        auto* node = s.ent[id].try_get_mut<UiPaint>();
                        if (node != nullptr) {
                            node->radius = v;
                        }
                    }  else {
                        auto* node = s.ent[id].try_get_mut<UiPaint>();
                        if (node != nullptr) {
                            node->icon = static_cast<uint8_t>(v);
                        }
                    }
                }
            } else if (strSame(key, "children") && eat(c, '[')) {
                if (!eat(c, ']')) {
                    for (;;) {
                        parseNode(s, id, c);
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
    if (textLen > 0) {
        uiText(s, id, text);
    }
    auto* flexNode = s.ent[id].try_get_mut<UiFlex>();
    if (flexNode != nullptr) {
        if (sawW) {
            uiFlexApplyAttr(*flexNode, "w", static_cast<uint8_t>(wMode), width);
        }
        if (sawH) {
            uiFlexApplyAttr(*flexNode, "h", static_cast<uint8_t>(hMode), height);
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

