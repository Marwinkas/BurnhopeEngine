#include "ui/State.hpp"

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
                            node->role = static_cast<uint8_t>(UiRole::Label);
                            node->r = 0.90f;
                            node->g = 0.92f;
                            node->b = 0.96f;
                        } else if (std::strcmp(role, "field") == 0) {
                            node->role = static_cast<uint8_t>(UiRole::Field);
                        } else if (std::strcmp(role, "check") == 0) {
                            node->role = static_cast<uint8_t>(UiRole::Check);
                        } else {
                            node->role = static_cast<uint8_t>(UiRole::Panel);
                        }
                    }
                }
            } else if (same(key, "dir")) {
                char dir[24]{};
                uint8_t n = 0;
                if (parseString(c, dir, 24, n)) {
                    auto* node = s.ent[id].try_get_mut<UiFlex>();
                    if (node != nullptr) {
                        if (std::strcmp(dir, "row") == 0) {
                            node->direction = 1;
                        } else if (std::strcmp(dir, "column-reverse") == 0) {
                            node->direction = 2;
                        } else if (std::strcmp(dir, "row-reverse") == 0) {
                            node->direction = 3;
                        } else {
                            node->direction = 0;
                        }
                    }
                }
            } else if (same(key, "wrap") || same(key, "justify") || same(key, "align") || same(key, "content") || same(key, "self") || same(key, "overflow") || same(key, "box") || same(key, "position")) {
                char word[16]{};
                uint8_t n = 0;
                if (parseString(c, word, 16, n)) {
                    auto* node = s.ent[id].try_get_mut<UiFlex>();
                    if (node != nullptr && same(key, "wrap")) {
                        node->wrap = std::strcmp(word, "wrap") == 0 ? 1 : std::strcmp(word, "reverse") == 0 ? 2 : 0;
                    } else if (node != nullptr && same(key, "justify")) {
                        uint8_t v = 0;
                        if (std::strcmp(word, "center") == 0) {
                            v = 1;
                        } else if (std::strcmp(word, "end") == 0) {
                            v = 2;
                        } else if (std::strcmp(word, "between") == 0) {
                            v = 3;
                        } else if (std::strcmp(word, "around") == 0) {
                            v = 4;
                        } else if (std::strcmp(word, "evenly") == 0) {
                            v = 5;
                        }
                        node->justify = v;
                    } else if (node != nullptr && (same(key, "align") || same(key, "content"))) {
                        uint8_t v = 0;
                        if (std::strcmp(word, "center") == 0) {
                            v = 1;
                        } else if (std::strcmp(word, "start") == 0) {
                            v = 2;
                        } else if (std::strcmp(word, "end") == 0) {
                            v = 3;
                        } else if (std::strcmp(word, "baseline") == 0) {
                            v = 4;
                        } else if (std::strcmp(word, "between") == 0) {
                            v = 4;
                        } else if (std::strcmp(word, "around") == 0) {
                            v = 5;
                        } else if (std::strcmp(word, "evenly") == 0) {
                            v = 6;
                        }
                        if (same(key, "align")) {
                            if (std::strcmp(word, "between") == 0 || std::strcmp(word, "around") == 0 || std::strcmp(word, "evenly") == 0) {
                                v = 0;
                            }
                            node->align = v;
                        } else {
                            if (std::strcmp(word, "baseline") == 0) {
                                v = 0;
                            }
                            node->content = v;
                        }
                    } else if (node != nullptr && same(key, "self")) {
                        uint8_t v = 0;
                        if (std::strcmp(word, "stretch") == 0) {
                            v = 1;
                        } else if (std::strcmp(word, "center") == 0) {
                            v = 2;
                        } else if (std::strcmp(word, "start") == 0) {
                            v = 3;
                        } else if (std::strcmp(word, "end") == 0) {
                            v = 4;
                        } else if (std::strcmp(word, "baseline") == 0) {
                            v = 5;
                        }
                        node->self = v;
                    } else if (node != nullptr && same(key, "overflow")) {
                        node->overflow = std::strcmp(word, "hidden") == 0 ? 1 : std::strcmp(word, "scroll") == 0 ? 2 : 0;
                    } else if (node != nullptr && same(key, "box")) {
                        node->box = std::strcmp(word, "content") == 0 ? 1 : 0;
                    } else if (node != nullptr && same(key, "position")) {
                        node->position = std::strcmp(word, "absolute") == 0 ? 1 : std::strcmp(word, "relative") == 0 ? 2 : 0;
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
            } else if (same(key, "basis")) {
                uint8_t mode = 0;
                float value = 0.0f;
                readSize(c, mode, value);
                auto* node = s.ent[id].try_get_mut<UiFlex>();
                if (node != nullptr) {
                    node->basisMode = mode;
                    node->basis = value;
                }
            } else if (same(key, "maxW") || same(key, "maxH")) {
                uint8_t mode = 0;
                float value = 0.0f;
                readSize(c, mode, value);
                auto* node = s.ent[id].try_get_mut<UiFlex>();
                if (node != nullptr && same(key, "maxW")) {
                    node->maxWMode = mode;
                    node->maxW = value;
                } else if (node != nullptr) {
                    node->maxHMode = mode;
                    node->maxH = value;
                }
            } else if (same(key, "gapRow") || same(key, "aspect") || same(key, "right") || same(key, "bottom")) {
                float v = 0.0f;
                if (parseNumber(c, v)) {
                    auto* node = s.ent[id].try_get_mut<UiFlex>();
                    if (node != nullptr && same(key, "gapRow")) {
                        node->gapRow = v;
                    } else if (node != nullptr && same(key, "aspect")) {
                        node->aspect = v;
                    } else if (node != nullptr && same(key, "right")) {
                        node->posR = v;
                        node->inset = static_cast<uint8_t>(node->inset | 1);
                    } else if (node != nullptr) {
                        node->posB = v;
                        node->inset = static_cast<uint8_t>(node->inset | 2);
                    }
                }
            } else if (same(key, "grow")) {
                float v = 0.0f;
                if (parseNumber(c, v)) {
                    auto* node = s.ent[id].try_get_mut<UiFlex>();
                    if (node != nullptr) {
                        node->grow = v;
                        node->shrink = 1.0f;
                    }
                }
            } else if (same(key, "pad") || same(key, "gap") || same(key, "radius") || same(key, "tag") || same(key, "icon")) {
                float v = 0.0f;
                if (parseNumber(c, v)) {
                    if (same(key, "pad")) {
                        auto* node = s.ent[id].try_get_mut<UiFlex>();
                        if (node != nullptr) {
                            node->pad = v;
                        }
                    } else if (same(key, "gap")) {
                        auto* node = s.ent[id].try_get_mut<UiFlex>();
                        if (node != nullptr) {
                            node->gap = v;
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
            flexNode->widthMode = wMode;
            flexNode->width = width;
        }
        if (sawH) {
            flexNode->heightMode = hMode;
            flexNode->height = height;
        }
    }
    return id;
}

const char* kFallback = R"({
  "id": "json_card",
  "dir": "column",
  "pad": 10,
  "gap": 6,
  "radius": 8,
  "w": "100%",
  "fill": [0.16, 0.18, 0.24, 1],
  "children": [
    {"id": "json_title", "role": "label", "text": "JSON LAYOUT"},
    {"id": "json_note", "role": "label", "text": "embedded layout"},
    {"id": "json_go", "role": "button", "text": "JSON BTN", "w": 120, "h": 28, "fill": [0.28, 0.36, 0.52, 1]}
  ]
})";

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
        return uiImportJson(s, parent, kFallback);
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return uiImportJson(s, parent, kFallback);
    }
    char buf[16384];
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, file);
    std::fclose(file);
    buf[n] = '\0';
    if (n == 0) {
        return uiImportJson(s, parent, kFallback);
    }
    return uiImportJson(s, parent, buf);
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

bool arenaPush(UiState& s, const char* bytes, uint32_t n, uint32_t& off) {
    if (n > 1024u * 1024u) {
        return false;
    }
    uint32_t cap = s.jsonCap == 0 ? 4096u : s.jsonCap;
    while (s.jsonUsed + n + 1u > cap) {
        if (cap > 8u * 1024u * 1024u) {
            return false;
        }
        cap *= 2u;
    }
    if (cap != s.jsonCap) {
        char* next = static_cast<char*>(std::realloc(s.jsonArena, cap));
        if (next == nullptr) {
            return false;
        }
        s.jsonArena = next;
        s.jsonCap = cap;
    }
    off = s.jsonUsed;
    if (n > 0) {
        std::memcpy(s.jsonArena + off, bytes, n);
    }
    s.jsonArena[off + n] = '\0';
    s.jsonUsed += n + 1u;
    return true;
}

void linkChild(flecs::entity parent, flecs::entity child) {
    auto* p = parent.try_get_mut<JsonNode>();
    auto* c = child.try_get_mut<JsonNode>();
    if (p == nullptr || c == nullptr) {
        return;
    }
    c->parent = parent.id();
    c->next = 0;
    if (p->first == 0) {
        p->first = child.id();
        return;
    }
    flecs::world world = parent.world();
    uint64_t id = p->first;
    while (id != 0) {
        flecs::entity e = world.entity(static_cast<flecs::entity_t>(id));
        auto* node = e.try_get_mut<JsonNode>();
        if (node == nullptr) {
            return;
        }
        if (node->next == 0) {
            node->next = child.id();
            return;
        }
        id = node->next;
    }
}

bool parseDocString(Cur& c, UiState& s, uint32_t& off, uint32_t& len) {
    skip(c);
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    std::vector<char> buf;
    while (c.p < c.end && *c.p != '"') {
        unsigned char ch = static_cast<unsigned char>(*c.p++);
        if (ch == '\\' && c.p < c.end) {
            const char n = *c.p++;
            if (n == 'n') {
                ch = '\n';
            } else if (n == 't') {
                ch = '\t';
            } else if (n == 'r') {
                ch = '\r';
            } else if (n == 'u' && c.p + 4 <= c.end) {
                uint32_t cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const char h = *c.p++;
                    cp <<= 4;
                    if (h >= '0' && h <= '9') {
                        cp += static_cast<uint32_t>(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        cp += static_cast<uint32_t>(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        cp += static_cast<uint32_t>(h - 'A' + 10);
                    }
                }
                if (cp < 0x80) {
                    buf.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    buf.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    buf.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    buf.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    buf.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                continue;
            } else {
                ch = static_cast<unsigned char>(n);
            }
        }
        buf.push_back(static_cast<char>(ch));
        if (buf.size() > 1024u * 1024u) {
            return false;
        }
    }
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    len = static_cast<uint32_t>(buf.size());
    return arenaPush(s, buf.empty() ? "" : buf.data(), len, off);
}

bool parseDocNumber(Cur& c, double& out) {
    skip(c);
    char buf[64]{};
    int n = 0;
    if (c.p < c.end && (*c.p == '-' || *c.p == '+')) {
        buf[n++] = *c.p++;
    }
    bool any = false;
    while (c.p < c.end && n < 62) {
        const char ch = *c.p;
        const bool num = (ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-';
        if (!num) {
            break;
        }
        if ((ch == '+' || ch == '-') && n > 0 && buf[n - 1] != 'e' && buf[n - 1] != 'E') {
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
    out = std::strtod(buf, &end);
    return end != buf;
}

bool keyCopy(char* dst, const char* src, uint32_t n) {
    uint32_t i = 0;
    const uint32_t cap = 47;
    while (i < n && i < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
    return true;
}

bool parseValue(Cur& c, UiState& s, flecs::entity node, uint32_t depth, uint32_t& count);

bool parseMembers(Cur& c, UiState& s, flecs::entity parent, bool array, uint32_t depth, uint32_t& count) {
    skip(c);
    if (c.p < c.end && *c.p == (array ? ']' : '}')) {
        ++c.p;
        return true;
    }
    uint16_t order = 0;
    while (c.p < c.end) {
        if (count > 4096) {
            return false;
        }
        char key[48]{};
        if (array) {
            std::snprintf(key, sizeof(key), "%u", static_cast<unsigned>(order));
        } else {
            uint32_t off = 0;
            uint32_t len = 0;
            if (!parseDocString(c, s, off, len) || !eat(c, ':')) {
                return false;
            }
            keyCopy(key, s.jsonArena + off, len);
        }
        flecs::entity child = parent.child();
        JsonNode blank{};
        keyCopy(blank.key, key, 47);
        blank.order = order;
        child.set<JsonNode>(blank);
        linkChild(parent, child);
        ++count;
        if (!parseValue(c, s, child, depth + 1, count)) {
            return false;
        }
        ++order;
        skip(c);
        if (eat(c, ',')) {
            continue;
        }
        if (eat(c, array ? ']' : '}')) {
            return true;
        }
        return false;
    }
    return false;
}

bool parseValue(Cur& c, UiState& s, flecs::entity node, uint32_t depth, uint32_t& count) {
    if (depth > 64) {
        return false;
    }
    skip(c);
    if (c.p >= c.end) {
        return false;
    }
    auto* stored = node.try_get_mut<JsonNode>();
    if (stored == nullptr) {
        return false;
    }
    if (*c.p == '"') {
        uint32_t off = 0;
        uint32_t len = 0;
        if (!parseDocString(c, s, off, len)) {
            return false;
        }
        stored = node.try_get_mut<JsonNode>();
        stored->kind = static_cast<uint8_t>(JsonKind::String);
        stored->textOff = off;
        stored->textLen = len;
        return true;
    }
    if (*c.p == '{') {
        ++c.p;
        stored->kind = static_cast<uint8_t>(JsonKind::Object);
        return parseMembers(c, s, node, false, depth, count);
    }
    if (*c.p == '[') {
        ++c.p;
        stored->kind = static_cast<uint8_t>(JsonKind::Array);
        return parseMembers(c, s, node, true, depth, count);
    }
    if (c.end - c.p >= 4 && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        stored->kind = static_cast<uint8_t>(JsonKind::Bool);
        stored->boolean = 1;
        return true;
    }
    if (c.end - c.p >= 5 && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        stored->kind = static_cast<uint8_t>(JsonKind::Bool);
        stored->boolean = 0;
        return true;
    }
    if (c.end - c.p >= 4 && std::memcmp(c.p, "null", 4) == 0) {
        c.p += 4;
        stored->kind = static_cast<uint8_t>(JsonKind::Null);
        return true;
    }
    double number = 0;
    if (!parseDocNumber(c, number)) {
        return false;
    }
    stored->kind = static_cast<uint8_t>(JsonKind::Number);
    stored->number = number;
    return true;
}

void writeEscape(std::FILE* file, const char* text, uint32_t len) {
    std::fputc('"', file);
    for (uint32_t i = 0; i < len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '"' || ch == '\\') {
            std::fputc('\\', file);
            std::fputc(static_cast<char>(ch), file);
        } else if (ch == '\n') {
            std::fputs("\\n", file);
        } else if (ch == '\t') {
            std::fputs("\\t", file);
        } else if (ch < 0x20) {
            std::fprintf(file, "\\u%04x", ch);
        } else {
            std::fputc(static_cast<char>(ch), file);
        }
    }
    std::fputc('"', file);
}

bool writeNode(std::FILE* file, const UiState& s, flecs::entity e, int indent);

bool writeNode(std::FILE* file, const UiState& s, flecs::entity e, int indent) {
    const JsonNode* node = e.try_get<JsonNode>();
    if (node == nullptr) {
        return false;
    }
    const auto kind = static_cast<JsonKind>(node->kind);
    if (kind == JsonKind::Null) {
        std::fputs("null", file);
        return true;
    }
    if (kind == JsonKind::Bool) {
        std::fputs(node->boolean != 0 ? "true" : "false", file);
        return true;
    }
    if (kind == JsonKind::Number) {
        std::fprintf(file, "%.17g", node->number);
        return true;
    }
    if (kind == JsonKind::String) {
        if (s.jsonArena == nullptr || node->textOff + node->textLen > s.jsonUsed) {
            std::fputs("\"\"", file);
            return true;
        }
        writeEscape(file, s.jsonArena + node->textOff, node->textLen);
        return true;
    }
    const bool array = kind == JsonKind::Array;
    std::fputc(array ? '[' : '{', file);
    uint64_t id = node->first;
    bool any = false;
    while (id != 0) {
        flecs::entity child = s.world.entity(static_cast<flecs::entity_t>(id));
        if (child.id() == 0 || !child.is_alive()) {
            break;
        }
        const JsonNode* c = child.try_get<JsonNode>();
        if (c == nullptr) {
            break;
        }
        if (any) {
            std::fputc(',', file);
        }
        any = true;
        std::fputc('\n', file);
        for (int i = 0; i < indent + 1; ++i) {
            std::fputs("  ", file);
        }
        if (!array) {
            writeEscape(file, c->key, static_cast<uint32_t>(std::strlen(c->key)));
            std::fputs(": ", file);
        }
        if (!writeNode(file, s, child, indent + 1)) {
            return false;
        }
        id = c->next;
    }
    if (any) {
        std::fputc('\n', file);
        for (int i = 0; i < indent; ++i) {
            std::fputs("  ", file);
        }
    }
    std::fputc(array ? ']' : '}', file);
    return true;
}

void jsonRelease(UiState& s) {
    if (s.jsonDoc.id() != 0 && s.jsonDoc.is_alive()) {
        s.jsonDoc.destruct();
    }
    s.jsonDoc = {};
    std::free(s.jsonArena);
    s.jsonArena = nullptr;
    s.jsonUsed = 0;
    s.jsonCap = 0;
}

uint32_t jsonLoadFile(UiState& s, const char* path) {
    jsonRelease(s);
    if (path == nullptr) {
        return 0;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        spdlog::error("json open failed: {}", path);
        return 0;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return 0;
    }
    const long size = std::ftell(file);
    if (size <= 0 || size > 1024 * 1024) {
        std::fclose(file);
        spdlog::error("json size rejected: {}", path);
        return 0;
    }
    std::rewind(file);
    std::vector<char> buf(static_cast<size_t>(size) + 1u);
    const size_t n = std::fread(buf.data(), 1, static_cast<size_t>(size), file);
    std::fclose(file);
    buf[n] = '\0';
    Cur cur{buf.data(), buf.data() + n};
    s.jsonDoc = s.world.entity();
    JsonNode root{};
    root.kind = static_cast<uint8_t>(JsonKind::Null);
    s.jsonDoc.set<JsonNode>(root);
    uint32_t count = 1;
    if (!parseValue(cur, s, s.jsonDoc, 0, count)) {
        spdlog::error("json parse failed: {}", path);
        jsonRelease(s);
        return 0;
    }
    spdlog::info("json doc: {} flecs nodes from {}", count, path);
    return count;
}

bool jsonSaveFile(const UiState& s, const char* path) {
    if (path == nullptr || s.jsonDoc.id() == 0 || !s.jsonDoc.is_alive()) {
        return false;
    }
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        spdlog::error("json save failed: {}", path);
        return false;
    }
    const bool ok = writeNode(file, s, s.jsonDoc, 0);
    std::fputc('\n', file);
    std::fclose(file);
    return ok;
}

flecs::entity jsonFind(flecs::entity parent, const char* key) {
    if (parent.id() == 0 || !parent.is_alive() || key == nullptr) {
        return {};
    }
    const JsonNode* node = parent.try_get<JsonNode>();
    if (node == nullptr) {
        return {};
    }
    flecs::world world = parent.world();
    uint64_t id = node->first;
    while (id != 0) {
        flecs::entity child = world.entity(static_cast<flecs::entity_t>(id));
        if (child.id() == 0 || !child.is_alive()) {
            break;
        }
        const JsonNode* c = child.try_get<JsonNode>();
        if (c == nullptr) {
            break;
        }
        if (std::strcmp(c->key, key) == 0) {
            return child;
        }
        id = c->next;
    }
    return {};
}

flecs::entity jsonAt(const UiState& s, flecs::entity parent, uint32_t index) {
    if (parent.id() == 0 || !parent.is_alive()) {
        return {};
    }
    const JsonNode* node = parent.try_get<JsonNode>();
    if (node == nullptr) {
        return {};
    }
    uint64_t id = node->first;
    uint32_t i = 0;
    while (id != 0) {
        flecs::entity child = s.world.entity(static_cast<flecs::entity_t>(id));
        if (child.id() == 0 || !child.is_alive()) {
            break;
        }
        const JsonNode* c = child.try_get<JsonNode>();
        if (c == nullptr) {
            break;
        }
        if (i == index) {
            return child;
        }
        id = c->next;
        ++i;
    }
    return {};
}

bool jsonString(const UiState& s, flecs::entity e, const char*& data, uint32_t& len) {
    data = nullptr;
    len = 0;
    if (e.id() == 0 || !e.is_alive() || s.jsonArena == nullptr) {
        return false;
    }
    const JsonNode* node = e.try_get<JsonNode>();
    if (node == nullptr || node->kind != static_cast<uint8_t>(JsonKind::String)) {
        return false;
    }
    if (node->textOff + node->textLen > s.jsonUsed) {
        return false;
    }
    data = s.jsonArena + node->textOff;
    len = node->textLen;
    return true;
}

bool jsonNumber(flecs::entity e, double& out) {
    if (e.id() == 0 || !e.is_alive()) {
        return false;
    }
    const JsonNode* node = e.try_get<JsonNode>();
    if (node == nullptr || node->kind != static_cast<uint8_t>(JsonKind::Number)) {
        return false;
    }
    out = node->number;
    return true;
}

} // namespace burnhope
