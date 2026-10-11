#include "Json.hpp"

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
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\n' || *c.p == '\r' || *c.p == '\t')) {
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

} // namespace

bool arenaPush(JsonDocument& doc, const char* bytes, uint32_t n, uint32_t& off) {
    if (n > 1024u * 1024u) {
        return false;
    }
    uint32_t cap = doc.cap == 0 ? 4096u : doc.cap;
    while (doc.used + n + 1u > cap) {
        if (cap > 8u * 1024u * 1024u) {
            return false;
        }
        cap *= 2u;
    }
    if (cap != doc.cap) {
        char* next = static_cast<char*>(std::realloc(doc.arena, cap));
        if (next == nullptr) {
            return false;
        }
        doc.arena = next;
        doc.cap = cap;
    }
    off = doc.used;
    if (n > 0) {
        std::memcpy(doc.arena + off, bytes, n);
    }
    doc.arena[off + n] = '\0';
    doc.used += n + 1u;
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

bool parseDocString(Cur& c, JsonDocument& doc, uint32_t& off, uint32_t& len) {
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
    return arenaPush(doc, buf.empty() ? "" : buf.data(), len, off);
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

bool parseValue(Cur& c, JsonDocument& doc, flecs::entity node, uint32_t depth, uint32_t& count);

bool parseMembers(Cur& c, JsonDocument& doc, flecs::entity parent, bool array, uint32_t depth, uint32_t& count) {
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
            if (!parseDocString(c, doc, off, len) || !eat(c, ':')) {
                return false;
            }
            keyCopy(key, doc.arena + off, len);
        }
        flecs::entity child = parent.child();
        JsonNode blank{};
        keyCopy(blank.key, key, 47);
        blank.order = order;
        child.set<JsonNode>(blank);
        linkChild(parent, child);
        ++count;
        if (!parseValue(c, doc, child, depth + 1, count)) {
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

bool parseValue(Cur& c, JsonDocument& doc, flecs::entity node, uint32_t depth, uint32_t& count) {
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
        if (!parseDocString(c, doc, off, len)) {
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
        return parseMembers(c, doc, node, false, depth, count);
    }
    if (*c.p == '[') {
        ++c.p;
        stored->kind = static_cast<uint8_t>(JsonKind::Array);
        return parseMembers(c, doc, node, true, depth, count);
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

bool writeNode(std::FILE* file, const JsonDocument& doc, flecs::entity e, int indent);

bool writeNode(std::FILE* file, const JsonDocument& doc, flecs::entity e, int indent) {
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
        if (doc.arena == nullptr || node->textOff + node->textLen > doc.used) {
            std::fputs("\"\"", file);
            return true;
        }
        writeEscape(file, doc.arena + node->textOff, node->textLen);
        return true;
    }
    const bool array = kind == JsonKind::Array;
    std::fputc(array ? '[' : '{', file);
    uint64_t id = node->first;
    bool any = false;
    while (id != 0) {
        flecs::entity child = doc.world.entity(static_cast<flecs::entity_t>(id));
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
        if (!writeNode(file, doc, child, indent + 1)) {
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

void jsonRelease(JsonDocument& doc) {
    if (doc.root.id() != 0 && doc.root.is_alive()) {
        doc.root.destruct();
    }
    doc.root = {};
    std::free(doc.arena);
    doc.arena = nullptr;
    doc.used = 0;
    doc.cap = 0;
}

uint32_t jsonLoadFile(JsonDocument& doc, const char* path) {
    jsonRelease(doc);
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
    doc.root = doc.world.entity();
    JsonNode root{};
    root.kind = static_cast<uint8_t>(JsonKind::Null);
    doc.root.set<JsonNode>(root);
    uint32_t count = 1;
    if (!parseValue(cur, doc, doc.root, 0, count)) {
        spdlog::error("json parse failed: {}", path);
        jsonRelease(doc);
        return 0;
    }
    spdlog::info("json doc: {} flecs nodes from {}", count, path);
    return count;
}

bool jsonSaveFile(const JsonDocument& doc, const char* path) {
    if (path == nullptr || doc.root.id() == 0 || !doc.root.is_alive()) {
        return false;
    }
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        spdlog::error("json save failed: {}", path);
        return false;
    }
    const bool ok = writeNode(file, doc, doc.root, 0);
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

flecs::entity jsonAt(const JsonDocument& doc, flecs::entity parent, uint32_t index) {
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
        flecs::entity child = doc.world.entity(static_cast<flecs::entity_t>(id));
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

bool jsonString(const JsonDocument& doc, flecs::entity e, const char*& data, uint32_t& len) {
    data = nullptr;
    len = 0;
    if (e.id() == 0 || !e.is_alive() || doc.arena == nullptr) {
        return false;
    }
    const JsonNode* node = e.try_get<JsonNode>();
    if (node == nullptr || node->kind != static_cast<uint8_t>(JsonKind::String)) {
        return false;
    }
    if (node->textOff + node->textLen > doc.used) {
        return false;
    }
    data = doc.arena + node->textOff;
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
