#include "io/Json.hpp"
#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "Str.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {
namespace {

using json::Cur;
using json::eat;
using json::parseNumber;
using json::parseStringBody;
using json::parseStringCap;
using json::skip;

bool arenaEnsure(JsonDocument& doc, uint32_t extra) {
    if (extra > 1024u * 1024u) {
        return false;
    }
    uint32_t cap = doc.cap == 0 ? 4096u : doc.cap;
    while (doc.used + extra > cap) {
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
    return true;
}

uint32_t allocNode(JsonDocument& doc) {
    doc.nodes.push_back({});
    return static_cast<uint32_t>(doc.nodes.size());
}

void linkChild(JsonDocument& doc, uint32_t parent, uint32_t child) {
    if (parent == kJsonNone || parent > doc.nodes.size() || child == kJsonNone || child > doc.nodes.size()) {
        return;
    }
    JsonNode& p = doc.nodes[parent - 1];
    JsonNode& c = doc.nodes[child - 1];
    c.next = 0;
    if (p.first == 0) {
        p.first = child;
        p.last = child;
    } else {
        doc.nodes[p.last - 1].next = child;
        p.last = child;
    }
}

bool parseDocString(Cur& c, JsonDocument& doc, uint32_t& off, uint32_t& len) {
    skip(c);
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    if (!arenaEnsure(doc, 1u)) {
        return false;
    }
    const uint32_t start = doc.used;
    uint32_t written = 0;
    if (!parseStringBody(c, [&](char ch) {
            if (doc.used >= doc.cap && !arenaEnsure(doc, 256u)) {
                return false;
            }
            doc.arena[doc.used++] = ch;
            return true;
        }, 1024u * 1024u, written)) {
        return false;
    }
    if (!arenaEnsure(doc, 1u)) {
        return false;
    }
    len = written;
    off = start;
    doc.arena[doc.used++] = '\0';
    return true;
}

bool parseValue(Cur& c, JsonDocument& doc, uint32_t node, uint32_t depth, uint32_t& count);

bool parseMembers(Cur& c, JsonDocument& doc, uint32_t parent, bool array, uint32_t depth, uint32_t& count) {
    skip(c);
    if (c.p < c.end && *c.p == (array ? ']' : '}')) {
        ++c.p;
        return true;
    }
    uint16_t order = 0;
    while (c.p < c.end) {
        const uint32_t child = allocNode(doc);
        JsonNode& blank = doc.nodes[child - 1];
        if (array) {
            blank.key[0] = '\0';
        } else {
            uint32_t keyLen = 0;
            if (!parseStringCap(c, blank.key, sizeof(blank.key), keyLen) || !eat(c, ':')) {
                return false;
            }
        }
        blank.order = order;
        linkChild(doc, parent, child);
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

bool parseValue(Cur& c, JsonDocument& doc, uint32_t node, uint32_t depth, uint32_t& count) {
    if (depth > 64 || node == kJsonNone || node > doc.nodes.size()) {
        return false;
    }
    skip(c);
    if (c.p >= c.end) {
        return false;
    }
    JsonNode& stored = doc.nodes[node - 1];
    if (*c.p == '"') {
        uint32_t off = 0;
        uint32_t len = 0;
        if (!parseDocString(c, doc, off, len)) {
            return false;
        }
        stored.kind = static_cast<uint8_t>(JsonKind::String);
        stored.textOff = off;
        stored.textLen = len;
        return true;
    }
    if (*c.p == '{') {
        ++c.p;
        stored.kind = static_cast<uint8_t>(JsonKind::Object);
        return parseMembers(c, doc, node, false, depth, count);
    }
    if (*c.p == '[') {
        ++c.p;
        stored.kind = static_cast<uint8_t>(JsonKind::Array);
        return parseMembers(c, doc, node, true, depth, count);
    }
    if (c.end - c.p >= 4 && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        stored.kind = static_cast<uint8_t>(JsonKind::Bool);
        stored.boolean = 1;
        return true;
    }
    if (c.end - c.p >= 5 && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        stored.kind = static_cast<uint8_t>(JsonKind::Bool);
        stored.boolean = 0;
        return true;
    }
    if (c.end - c.p >= 4 && std::memcmp(c.p, "null", 4) == 0) {
        c.p += 4;
        stored.kind = static_cast<uint8_t>(JsonKind::Null);
        return true;
    }
    double number = 0;
    if (!parseNumber(c, number)) {
        return false;
    }
    stored.kind = static_cast<uint8_t>(JsonKind::Number);
    stored.number = number;
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

bool writeNode(std::FILE* file, const JsonDocument& doc, uint32_t node, int indent);

bool writeNode(std::FILE* file, const JsonDocument& doc, uint32_t node, int indent) {
    if (node == kJsonNone || node > doc.nodes.size()) {
        return false;
    }
    const JsonNode& n = doc.nodes[node - 1];
    const auto kind = static_cast<JsonKind>(n.kind);
    if (kind == JsonKind::Null) {
        std::fputs("null", file);
        return true;
    }
    if (kind == JsonKind::Bool) {
        std::fputs(n.boolean != 0 ? "true" : "false", file);
        return true;
    }
    if (kind == JsonKind::Number) {
        std::fprintf(file, "%.17g", n.number);
        return true;
    }
    if (kind == JsonKind::String) {
        if (doc.arena == nullptr || n.textOff + n.textLen > doc.used) {
            std::fputs("\"\"", file);
            return true;
        }
        writeEscape(file, doc.arena + n.textOff, n.textLen);
        return true;
    }
    const bool array = kind == JsonKind::Array;
    std::fputc(array ? '[' : '{', file);
    uint32_t id = n.first;
    bool any = false;
    while (id != kJsonNone) {
        if (id > doc.nodes.size()) {
            break;
        }
        const JsonNode& c = doc.nodes[id - 1];
        if (any) {
            std::fputc(',', file);
        }
        any = true;
        std::fputc('\n', file);
        for (int i = 0; i < indent + 1; ++i) {
            std::fputs("  ", file);
        }
        if (!array) {
            writeEscape(file, c.key, static_cast<uint32_t>(std::strlen(c.key)));
            std::fputs(": ", file);
        }
        if (!writeNode(file, doc, id, indent + 1)) {
            return false;
        }
        id = c.next;
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

} // namespace

void jsonRelease(JsonDocument& doc) {
    doc.nodes.clear();
    doc.root = kJsonNone;
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
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped)) {
        spdlog::error("json open failed: {}", path);
        return 0;
    }
    if (mapped.size == 0 || mapped.size > 1024u * 1024u) {
        fileUnmap(&mapped);
        spdlog::error("json size rejected: {}", path);
        return 0;
    }
    const char* begin = reinterpret_cast<const char*>(mapped.data);
    Cur cur{begin, begin + mapped.size};
    doc.root = allocNode(doc);
    JsonNode& root = doc.nodes[doc.root - 1];
    root.kind = static_cast<uint8_t>(JsonKind::Null);
    uint32_t count = 1;
    const bool ok = parseValue(cur, doc, doc.root, 0, count);
    fileUnmap(&mapped);
    if (!ok) {
        spdlog::error("json parse failed: {}", path);
        jsonRelease(doc);
        return 0;
    }
    spdlog::info("json doc: {} nodes from {}", count, path);
    return count;
}

bool jsonSaveFile(const JsonDocument& doc, const char* path) {
    if (path == nullptr || doc.root == kJsonNone) {
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

uint32_t jsonFind(const JsonDocument& doc, uint32_t parent, const char* key) {
    if (parent == kJsonNone || parent > doc.nodes.size() || key == nullptr) {
        return kJsonNone;
    }
    const JsonNode& node = doc.nodes[parent - 1];
    uint32_t id = node.first;
    while (id != kJsonNone) {
        if (id > doc.nodes.size()) {
            break;
        }
        const JsonNode& c = doc.nodes[id - 1];
        if (std::strcmp(c.key, key) == 0) {
            return id;
        }
        id = c.next;
    }
    return kJsonNone;
}

uint32_t jsonAt(const JsonDocument& doc, uint32_t parent, uint32_t index) {
    if (parent == kJsonNone || parent > doc.nodes.size()) {
        return kJsonNone;
    }
    const JsonNode& node = doc.nodes[parent - 1];
    uint32_t id = node.first;
    uint32_t i = 0;
    while (id != kJsonNone) {
        if (id > doc.nodes.size()) {
            break;
        }
        if (i == index) {
            return id;
        }
        id = doc.nodes[id - 1].next;
        ++i;
    }
    return kJsonNone;
}

bool jsonString(const JsonDocument& doc, uint32_t node, const char*& data, uint32_t& len) {
    data = nullptr;
    len = 0;
    if (node == kJsonNone || node > doc.nodes.size() || doc.arena == nullptr) {
        return false;
    }
    const JsonNode& n = doc.nodes[node - 1];
    if (n.kind != static_cast<uint8_t>(JsonKind::String)) {
        return false;
    }
    if (n.textOff + n.textLen > doc.used) {
        return false;
    }
    data = doc.arena + n.textOff;
    len = n.textLen;
    return true;
}

bool jsonNumber(const JsonDocument& doc, uint32_t node, double& out) {
    if (node == kJsonNone || node > doc.nodes.size()) {
        return false;
    }
    const JsonNode& n = doc.nodes[node - 1];
    if (n.kind != static_cast<uint8_t>(JsonKind::Number)) {
        return false;
    }
    out = n.number;
    return true;
}

} // namespace burnhope
