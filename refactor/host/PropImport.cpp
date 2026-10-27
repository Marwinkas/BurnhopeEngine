#include "host/PropImport.hpp"
#include "host/PropImportInternal.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {
namespace {

using namespace propimport;

// Builds the node object at `c`: read every key, spawn the node, then build its children. Because the keys are
// all read before anything is spawned, their order in the document never matters.
bool parseNode(Cur& c, Import& in, uint16_t parent);

bool parseChildren(Cur& c, Import& in, uint16_t parent) {
    if (!json::eat(c, '[')) {
        return fail(in, c.p, "expected a children array");
    }
    json::skip(c);
    while (c.p < c.end && *c.p != ']') {
        if (!parseNode(c, in, parent)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, ']') || fail(in, c.p, "unterminated children array");
}

bool parseNode(Cur& c, Import& in, uint16_t parent) {
    const char* nodeAt = c.p;
    Node node{};
    const char* children = nullptr;
    if (!parseNodeKeys(c, in, node, children)) {
        return false;
    }
    if (node.role[0] == '\0' && node.use[0] == '\0') {
        return children == nullptr && node.name[0] == '\0' ? true : fail(in, nodeAt, "node has no role");
    }
    uint16_t host = kUiNone;
    if (!spawnRole(in, parent, node, host)) {
        return fail(in, nodeAt, "unknown role", node.role[0] != '\0' ? node.role : node.use);
    }
    rememberShow(in, host, node);
    if (children != nullptr) {
        Cur inner{children, c.end};
        if (!parseChildren(inner, in, host)) {
            return false;
        }
    }
    finishNode(in, host, node);
    return true;
}

} // namespace

bool propImport(
    UiState& state,
    uint16_t parent,
    std::string_view text,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    void* user,
    const PropRole* roles,
    uint16_t roleCount) {
    return propImportNamed(state, parent, text, nullptr, key, bind, slots, slotCount, commands, commandCount, hidden, onFlag, user, roles, roleCount);
}

bool propImportNamed(
    UiState& state,
    uint16_t parent,
    std::string_view text,
    const char* asset,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    void* user,
    const PropRole* roles,
    uint16_t roleCount) {
    if (key == nullptr || text.data() == nullptr) {
        return false;
    }
    Import in{
        .state = &state,
        .asset = asset,
        .text = text.data(),
        .bind = &bind,
        .slots = slots,
        .slotCount = slotCount,
        .commands = commands,
        .commandCount = commandCount,
        .hidden = hidden,
        .onFlag = onFlag,
        .user = user,
        .roles = roles,
        .roleCount = roleCount,
    };
    Cur c{text.data(), text.data() + text.size()};
    if (!json::eat(c, '{')) {
        return fail(in, c.p, "a layout document is an object");
    }
    bool found = false;
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char name[16]{};
        uint32_t n = 0;
        const char* at = c.p;
        if (!json::parseStringCap(c, name, sizeof(name), n) || !json::eat(c, ':')) {
            return fail(in, at, "expected a layout name");
        }
        if (std::strcmp(name, key) == 0) {
            if (!parseChildren(c, in, parent)) {
                return false;
            }
            found = true;
        } else if (!json::skipValue(c)) {
            return fail(in, c.p, "malformed value of", name);
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return found || fail(in, text.data(), "layout not found:", key);
}

bool propImportAsset(
    UiState& state,
    uint16_t parent,
    const char* asset,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    void* user,
    const PropRole* roles,
    uint16_t roleCount) {
    char path[512]{};
    if (asset == nullptr || !platformResolveAsset(asset, path, sizeof(path))) {
        spdlog::error("layout {} was not found", asset != nullptr ? asset : "");
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        spdlog::error("layout {} did not map", path);
        return false;
    }
    const std::string_view text{reinterpret_cast<const char*>(file.data), file.size};
    const bool ok = propImportNamed(state, parent, text, asset, key, bind, slots, slotCount, commands, commandCount, hidden, onFlag, user, roles, roleCount);
    fileUnmap(&file);
    return ok;
}

} // namespace burnhope
