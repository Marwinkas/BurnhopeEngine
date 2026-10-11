#include "ui/LayoutBridge.hpp"
#include "ui/State.hpp"
#include "ui/widget/Detail.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace burnhope {
namespace {

struct Cur {
    const char* p = nullptr;
    const char* end = nullptr;
};

void skip(Cur& c) {
    while (c.p < c.end) {
        const char ch = *c.p;
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            break;
        }
        ++c.p;
    }
}

bool eat(Cur& c, char ch) {
    skip(c);
    if (c.p < c.end && *c.p == ch) {
        ++c.p;
        return true;
    }
    return false;
}

bool parseString(Cur& c, char* dst, uint8_t cap, uint8_t& len) {
    skip(c);
    if (c.p >= c.end || *c.p != '"' || cap == 0) {
        return false;
    }
    ++c.p;
    len = 0;
    while (c.p < c.end && *c.p != '"') {
        char ch = *c.p++;
        if (ch == '\\' && c.p < c.end) {
            const char n = *c.p++;
            ch = n == 'n' ? '\n' : n == 't' ? '\t' : n;
        }
        if (static_cast<uint8_t>(len + 1) < cap) {
            dst[len++] = ch;
        }
    }
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    dst[len] = '\0';
    return true;
}

bool parseNumber(Cur& c, float& out) {
    skip(c);
    char buf[48]{};
    int n = 0;
    if (c.p < c.end && (*c.p == '-' || *c.p == '+')) {
        buf[n++] = *c.p++;
    }
    bool any = false;
    while (c.p < c.end && n < 46) {
        const char ch = *c.p;
        const bool num = (ch >= '0' && ch <= '9') || ch == '.';
        if (!num) {
            break;
        }
        buf[n++] = ch;
        ++c.p;
        any = true;
    }
    if (!any) {
        return false;
    }
    buf[n] = '\0';
    char* end = nullptr;
    out = std::strtof(buf, &end);
    return end != buf;
}

void skipValue(Cur& c) {
    skip(c);
    if (c.p >= c.end) {
        return;
    }
    if (*c.p == '"') {
        char tmp[4]{};
        uint8_t n = 0;
        parseString(c, tmp, 4, n);
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
            if (!parseString(c, key, 32, kl) || !eat(c, ':')) {
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
    parseNumber(c, dummy);
}

bool same(const char* key, const char* name) {
    return std::strcmp(key, name) == 0;
}

void bindName(UiState& s, uint16_t id, const char* name) {
    uiName(s, id, name);
}

void readSize(Cur& c, uint8_t& mode, float& value) {
    skip(c);
    if (c.p < c.end && *c.p == '"') {
        char buf[16]{};
        uint8_t n = 0;
        if (!parseString(c, buf, 16, n)) {
            return;
        }
        if (n > 0 && buf[n - 1] == '%') {
            buf[n - 1] = '\0';
            mode = static_cast<uint8_t>(UiSize::Percent);
            value = std::strtof(buf, nullptr);
        } else {
            mode = static_cast<uint8_t>(UiSize::Px);
            value = std::strtof(buf, nullptr);
        }
        return;
    }
    float v = 0.0f;
    if (parseNumber(c, v)) {
        mode = static_cast<uint8_t>(UiSize::Px);
        value = v;
    }
}

uint16_t parseNode(UiState& s, uint16_t parent, Cur& c) {
    if (!eat(c, '{')) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Panel);
    paint.r = 0.16f;
    paint.g = 0.18f;
    paint.b = 0.22f;
    paint.a = 1.0f;
    paint.tag = -1;
    const uint16_t id = uiNode(s, parent, flex, paint);
    if (id == kUiNone) {
        skipValue(c);
        return kUiNone;
    }
    char text[96]{};
    uint8_t textLen = 0;
    uint8_t wMode = 0;
    uint8_t hMode = 0;
    float width = 0.0f;
    float height = 0.0f;
    bool sawW = false;
    bool sawH = false;
    skip(c);
    if (!eat(c, '}')) {
        for (;;) {
            char key[32]{};
            uint8_t kl = 0;
            if (!parseString(c, key, 32, kl) || !eat(c, ':')) {
                break;
            }
            if (same(key, "id")) {
                char name[32]{};
                uint8_t n = 0;
                if (parseString(c, name, 32, n)) {
                    bindName(s, id, name);
                }
            } else if (same(key, "text")) {
                parseString(c, text, 96, textLen);
            } else if (same(key, "role")) {
                char role[16]{};
                uint8_t n = 0;
                if (parseString(c, role, 16, n)) {
                    auto* node = s.ent[id].try_get_mut<UiPaint>();
                    if (node != nullptr) {
                        if (std::strcmp(role, "button") == 0) {
                            node->role = static_cast<uint8_t>(UiRole::Button);
                        } else if (std::strcmp(role, "label") == 0) {
                            const UiPaint ink = paintToken(s.look, UiRole::Label, UiToken::Text, 0.0f);
                            node->role = ink.role;
                            node->token = ink.token;
                            node->r = ink.r;
                            node->g = ink.g;
                            node->b = ink.b;
                        } else if (std::strcmp(role, "field") == 0) {
                            node->role = static_cast<uint8_t>(UiRole::Field);
                        } else if (std::strcmp(role, "check") == 0) {
                            node->role = static_cast<uint8_t>(UiRole::Check);
                        } else {
                            const UiPaint ink = paintToken(s.look, UiRole::Panel, UiToken::Content, 0.0f);
                            node->role = ink.role;
                            node->token = ink.token;
                            node->r = ink.r;
                            node->g = ink.g;
                            node->b = ink.b;
                        }
                    }
                }
            } else if (same(key, "dir") || same(key, "wrap") || same(key, "justify") || same(key, "align") || same(key, "content") || same(key, "self") || same(key, "overflow") || same(key, "box") || same(key, "position")) {
                char word[24]{};
                uint8_t n = 0;
                if (parseString(c, word, 24, n)) {
                    if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                        uiFlexApplyAttr(*node, key, word);
                    }
                }
            } else if (same(key, "fill") && eat(c, '[')) {
                float chans[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                int n = 0;
                for (;;) {
                    float v = 0.0f;
                    if (n < 4 && parseNumber(c, v)) {
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
            } else if (same(key, "w")) {
                readSize(c, wMode, width);
                sawW = true;
            } else if (same(key, "h")) {
                readSize(c, hMode, height);
                sawH = true;
            } else if (same(key, "basis") || same(key, "maxW") || same(key, "maxH")) {
                uint8_t mode = 0;
                float value = 0.0f;
                readSize(c, mode, value);
                if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                    uiFlexApplyAttr(*node, key, mode, value);
                }
            } else if (same(key, "gapRow") || same(key, "aspect") || same(key, "right") || same(key, "bottom") || same(key, "grow")) {
                float v = 0.0f;
                if (parseNumber(c, v)) {
                    if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                        uiFlexApplyAttr(*node, key, v);
                    }
                }
            } else if (same(key, "pad") || same(key, "gap") || same(key, "radius") || same(key, "tag") || same(key, "icon")) {
                float v = 0.0f;
                if (parseNumber(c, v)) {
                    if (same(key, "pad") || same(key, "gap")) {
                        if (auto* node = s.ent[id].try_get_mut<UiFlex>()) {
                            uiFlexApplyAttr(*node, key, v);
                        }
                    } else if (same(key, "radius")) {
                        auto* node = s.ent[id].try_get_mut<UiPaint>();
                        if (node != nullptr) {
                            node->radius = v;
                        }
                    } else if (same(key, "tag")) {
                        auto* node = s.ent[id].try_get_mut<UiPaint>();
                        if (node != nullptr) {
                            node->tag = static_cast<int8_t>(v);
                        }
                    } else {
                        auto* node = s.ent[id].try_get_mut<UiPaint>();
                        if (node != nullptr) {
                            node->icon = static_cast<uint8_t>(v);
                        }
                    }
                }
            } else if (same(key, "children") && eat(c, '[')) {
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
    const UiPaint* node = s.ent[id].try_get<UiPaint>();
    const uint8_t role = node != nullptr ? node->role : 0;
    if (textLen > 0 && role == static_cast<uint8_t>(UiRole::Field)) {
        UiField field{};
        uint8_t n = textLen < 95 ? textLen : 95;
        std::memcpy(field.bytes, text, n);
        field.bytes[n] = '\0';
        field.len = n;
        field.caret = n;
        field.anchor = n;
        s.ent[id].set<UiField>(field);
    } else if (textLen > 0 && role == static_cast<uint8_t>(UiRole::Button)) {
        UiText label{};
        uint8_t n = textLen < 31 ? textLen : 31;
        std::memcpy(label.bytes, text, n);
        label.bytes[n] = '\0';
        label.len = n;
        s.ent[id].set<UiText>(label);
    } else if (textLen > 0) {
        uiText(s, id, text);
    } else if (role == static_cast<uint8_t>(UiRole::Check)) {
        s.ent[id].set<UiCheck>({});
    }
    auto* flexNode = s.ent[id].try_get_mut<UiFlex>();
    if (flexNode != nullptr) {
        if (sawW) {
            uiFlexApplyAttr(*flexNode, "w", wMode, width);
        }
        if (sawH) {
            uiFlexApplyAttr(*flexNode, "h", hMode, height);
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
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        spdlog::error("ui: failed to read json file {}", path);
        return kUiNone;
    }
    std::fseek(file, 0, SEEK_END);
    const long sz = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (sz <= 0 || sz > 8 * 1024 * 1024) {
        std::fclose(file);
        spdlog::error("ui: failed to read json file {}", path);
        return kUiNone;
    }
    std::vector<char> buf(static_cast<size_t>(sz) + 1);
    const size_t n = std::fread(buf.data(), 1, static_cast<size_t>(sz), file);
    std::fclose(file);
    buf[n] = '\0';
    return uiImportJson(s, parent, buf.data());
}

uint16_t uiFindName(const UiState& s, const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return kUiNone;
    }
    const ecs_entity_t symbol = ecs_lookup_symbol(s.world, name, false, false);
    const flecs::entity named = symbol != 0 ? flecs::entity(s.world, symbol) : s.world.lookup(name);
    if (named.id() != 0 && named.is_alive()) {
        const UiSlot* slot = named.try_get<UiSlot>();
        if (slot != nullptr) {
            return slot->index;
        }
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiName* stored = s.ent[id].try_get<UiName>();
        if (stored != nullptr && std::strcmp(stored->bytes, name) == 0) {
            return id;
        }
    }
    return kUiNone;
}

} // namespace burnhope

