#include "gui/loader/GuiLoader.hpp"

#include "gui/data/GuiIcons.hpp"

#include <algorithm>
#include <cstring>

#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/style/GuiStyleResolver.hpp"
#include "io/File.hpp"
#include "io/JsonCursor.hpp"

namespace burnhope::gui {
namespace {

enum Behavior : uint32_t {
    kBhHover = 1u << 0,
    kBhClick = 1u << 1,
    kBhDrag = 1u << 2,
    kBhScroll = 1u << 3,
    kBhFocus = 1u << 4,
    kBhEdit = 1u << 5,
    kBhVirtual = 1u << 6,
    kBhOverlay = 1u << 7,
};

struct NodeDesc {
    char cls[kGuiStyleNameCap] = {};
    char name[kGuiStyleNameCap] = {};
    char text[kGuiTextCap] = {};
    char toggles[kGuiTogglePathCap] = {};
    char tooltip[kGuiTextCap] = {};
    char bind[32] = {};
    bool tooltipHost = false;
    char itemClass[kGuiStyleNameCap] = {};
    uint8_t textLen = 0;
    int8_t align = -1;
    int16_t size = -1;
    int16_t photo = -1;
    uint8_t icon = 0;
    bool hasColor = false;
    bool hidden = false;
    bool multiline = false;
    bool flat = false;
    bool passive = false;
    uint8_t axis = 0;
    uint8_t mode = 0;
    uint8_t maxLen = 127;
    uint8_t placement = 0;
    uint8_t layer = 0;
    uint8_t dismiss = 1;
    uint32_t behaviors = 0;
    uint32_t action = 0;
    uint32_t count = 0;
    uint32_t itemAction = 0;
    uint32_t columns = 1;
    float itemW = 0.0f;
    float itemH = 0.0f;
    float value = 0.0f;
    float minV = 0.0f;
    float maxV = 1.0f;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    json::Cur children = {};
    bool hasChildren = false;
};

struct PendingToggle {
    uint64_t entity = 0;
    char path[kGuiTogglePathCap] = {};
};

constexpr uint32_t kGuiIncludeDepthCap = 4;
constexpr uint32_t kGuiIncludePathCap = 128;

struct LoadCtx {
    uint32_t includeDepth = 0;
    flecs::world* world = nullptr;
    const GuiStyleSheet* sheet = nullptr;
    flecs::entity root;
    PendingToggle toggles[kGuiToggleCap] = {};
    uint32_t toggleCount = 0;
};

bool readNumber(json::Cur& c, double& out) {
    return json::parseNumber(c, out);
}

bool readBool(json::Cur& c, bool& out) {
    json::skip(c);
    const auto left = static_cast<size_t>(c.end - c.p);
    if (left >= 4 && std::memcmp(c.p, "true", 4) == 0) {
        c.p += 4;
        out = true;
        return true;
    }
    if (left >= 5 && std::memcmp(c.p, "false", 5) == 0) {
        c.p += 5;
        out = false;
        return true;
    }
    double n = 0.0;
    if (!readNumber(c, n)) {
        return false;
    }
    out = n != 0.0;
    return true;
}

// Reads a JSON string into dst (capacity cap incl. NUL), truncating on a codepoint boundary.
bool readString(json::Cur& c, char* dst, uint32_t cap, uint8_t& len) {
    json::skip(c);
    if (c.p >= c.end || *c.p != '"' || cap == 0) {
        return false;
    }
    ++c.p;
    uint32_t stored = 0;
    uint32_t written = 0;
    if (!json::parseStringBody(c, [&](char ch) {
            if (stored + 1 < cap) {
                dst[stored++] = ch;
            }
            return true;
        }, 0xFFFFu, written)) {
        return false;
    }
    if (stored < written && stored > 0) { // truncated: drop a multi-byte sequence the cut left incomplete
        uint32_t lead = stored - 1;
        while (lead > 0 && (static_cast<unsigned char>(dst[lead]) & 0xC0u) == 0x80u) {
            --lead;
        }
        const auto b0 = static_cast<unsigned char>(dst[lead]);
        const uint32_t need = b0 >= 0xF0u ? 4u : (b0 >= 0xE0u ? 3u : (b0 >= 0xC0u ? 2u : 1u));
        if (lead + need > stored) {
            stored = lead;
        }
    }
    dst[stored] = '\0';
    len = static_cast<uint8_t>(std::min<uint32_t>(stored, 255u));
    return true;
}

bool readKey(json::Cur& c, char* key, uint32_t cap) {
    json::skip(c);
    const json::Cur save = c;
    uint32_t len = 0;
    if (json::parseStringCap(c, key, cap, len)) {
        return true;
    }
    c = save; // longer than any known key: consume it and match nothing
    key[0] = '\0';
    return json::skipString(c);
}

bool isKey(const char* key, const char* name) {
    return std::strcmp(key, name) == 0;
}

uint8_t parseAlign(const char* s) {
    if (std::strcmp(s, "center") == 0) {
        return 1;
    }
    return std::strcmp(s, "right") == 0 ? 2 : 0;
}

bool readBehaviors(json::Cur& c, uint32_t& mask) {
    if (!json::eat(c, '[')) {
        return false;
    }
    json::skip(c);
    if (json::eat(c, ']')) {
        return true;
    }
    for (;;) {
        char word[16] = {};
        uint8_t len = 0;
        if (!readString(c, word, sizeof(word), len)) {
            return false;
        }
        if (isKey(word, "hover")) mask |= kBhHover;
        else if (isKey(word, "click")) mask |= kBhClick;
        else if (isKey(word, "drag")) mask |= kBhDrag;
        else if (isKey(word, "scroll")) mask |= kBhScroll;
        else if (isKey(word, "focus")) mask |= kBhFocus;
        else if (isKey(word, "edit")) mask |= kBhEdit;
        else if (isKey(word, "virtual")) mask |= kBhVirtual;
        else if (isKey(word, "overlay")) mask |= kBhOverlay;
        if (json::eat(c, ']')) {
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

bool readColor(json::Cur& c, float (&rgba)[4]) {
    if (!json::eat(c, '[')) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        double v = 0.0;
        if (!readNumber(c, v)) {
            return false;
        }
        rgba[i] = static_cast<float>(v);
        if (i < 3 && !json::eat(c, ',')) {
            return false;
        }
    }
    return json::eat(c, ']');
}

// One scalar key of a node. Returns false on a malformed value.
bool readField(json::Cur& c, const char* key, NodeDesc& d) {
    double n = 0.0;
    bool b = false;
    uint8_t len = 0;
    char word[16] = {};
    if (isKey(key, "class") || isKey(key, "type")) {
        return readString(c, d.cls, sizeof(d.cls), len);
    }
    if (isKey(key, "name")) {
        return readString(c, d.name, sizeof(d.name), len);
    }
    if (isKey(key, "icon")) {
        char iconName[24] = {};
        if (!readString(c, iconName, sizeof(iconName), len)) {
            return false;
        }
        d.icon = guiIconFind(iconName);
        return true;
    }
    if (isKey(key, "toggles")) {
        return readString(c, d.toggles, sizeof(d.toggles), len);
    }
    if (isKey(key, "tooltip")) {
        return readString(c, d.tooltip, sizeof(d.tooltip), len);
    }
    if (isKey(key, "bind")) {
        return readString(c, d.bind, sizeof(d.bind), len);
    }
    if (isKey(key, "tooltipHost")) {
        return readBool(c, d.tooltipHost);
    }
    if (isKey(key, "itemClass")) {
        return readString(c, d.itemClass, sizeof(d.itemClass), len);
    }
    if (isKey(key, "text")) {
        if (!readString(c, d.text, sizeof(d.text), len)) {
            return false;
        }
        d.textLen = std::min<uint8_t>(len, static_cast<uint8_t>(kGuiTextCap - 1));
        return true;
    }
    if (isKey(key, "align")) {
        json::skip(c);
        if (c.p < c.end && *c.p == '"') {
            if (!readString(c, word, sizeof(word), len)) {
                return false;
            }
            d.align = static_cast<int8_t>(parseAlign(word));
            return true;
        }
        if (!readNumber(c, n)) {
            return false;
        }
        d.align = static_cast<int8_t>(std::clamp(n, 0.0, 2.0));
        return true;
    }
    if (isKey(key, "axis")) {
        json::skip(c);
        if (c.p < c.end && *c.p == '"') {
            if (!readString(c, word, sizeof(word), len)) {
                return false;
            }
            d.axis = std::strcmp(word, "y") == 0 ? 1 : (std::strcmp(word, "xy") == 0 ? 2 : 0);
            return true;
        }
        if (!readNumber(c, n)) {
            return false;
        }
        d.axis = static_cast<uint8_t>(std::clamp(n, 0.0, 2.0));
        return true;
    }
    if (isKey(key, "placement")) {
        json::skip(c);
        if (c.p < c.end && *c.p == '"') {
            if (!readString(c, word, sizeof(word), len)) {
                return false;
            }
            d.placement = std::strcmp(word, "above") == 0 ? 1 : std::strcmp(word, "right") == 0 ? 2
                : std::strcmp(word, "left") == 0 ? 3 : std::strcmp(word, "pointer") == 0 ? 4 : 0;
            return true;
        }
        if (!readNumber(c, n)) {
            return false;
        }
        d.placement = static_cast<uint8_t>(std::clamp(n, 0.0, 4.0));
        return true;
    }
    if (isKey(key, "behaviors")) {
        return readBehaviors(c, d.behaviors);
    }
    if (isKey(key, "color")) {
        d.hasColor = readColor(c, d.color);
        return d.hasColor;
    }
    if (isKey(key, "hidden")) {
        return readBool(c, d.hidden);
    }
    if (isKey(key, "passive")) {
        return readBool(c, d.passive);
    }
    if (isKey(key, "flat")) {
        return readBool(c, d.flat);
    }
    if (isKey(key, "multiline")) {
        return readBool(c, d.multiline);
    }
    if (isKey(key, "dismiss")) {
        if (!readBool(c, b)) {
            return false;
        }
        d.dismiss = b ? 1 : 0;
        return true;
    }
    if (isKey(key, "children")) {
        d.children = c;
        d.hasChildren = true;
        return json::skipValue(c);
    }
    const bool numeric = isKey(key, "size") || isKey(key, "photo") || isKey(key, "action") || isKey(key, "count") || isKey(key, "itemAction") ||
        isKey(key, "itemW") || isKey(key, "itemH") || isKey(key, "columns") || isKey(key, "mode") ||
        isKey(key, "value") || isKey(key, "minV") || isKey(key, "maxV") || isKey(key, "maxLen") || isKey(key, "layer");
    if (!numeric) {
        return json::skipValue(c);
    }
    if (!readNumber(c, n)) {
        return false;
    }
    if (isKey(key, "size")) d.size = static_cast<int16_t>(std::clamp(n, 1.0, 255.0));
    else if (isKey(key, "photo")) d.photo = static_cast<int16_t>(std::clamp(n, 0.0, 254.0));
    else if (isKey(key, "action")) d.action = static_cast<uint32_t>(std::max(n, 0.0));
    else if (isKey(key, "count")) d.count = static_cast<uint32_t>(std::max(n, 0.0));
    else if (isKey(key, "itemAction")) d.itemAction = static_cast<uint32_t>(std::max(n, 0.0));
    else if (isKey(key, "itemW")) d.itemW = static_cast<float>(n);
    else if (isKey(key, "itemH")) d.itemH = static_cast<float>(n);
    else if (isKey(key, "columns")) d.columns = static_cast<uint32_t>(std::max(n, 1.0));
    else if (isKey(key, "mode")) d.mode = static_cast<uint8_t>(std::clamp(n, 0.0, 2.0));
    else if (isKey(key, "value")) d.value = static_cast<float>(n);
    else if (isKey(key, "minV")) d.minV = static_cast<float>(n);
    else if (isKey(key, "maxV")) d.maxV = static_cast<float>(n);
    else if (isKey(key, "maxLen")) d.maxLen = static_cast<uint8_t>(std::clamp(n, 1.0, 127.0));
    else d.layer = static_cast<uint8_t>(std::clamp(n, 0.0, 255.0));
    return true;
}

void applyBehaviors(flecs::entity e, const NodeDesc& d, uint32_t itemClass) {
    const uint32_t b = d.behaviors;
    if ((b & (kBhHover | kBhEdit)) != 0) {
        e.add<UiHoverable>();
    }
    if ((b & (kBhFocus | kBhEdit)) != 0) {
        e.add<UiFocusable>();
    }
    if ((b & kBhClick) != 0) {
        e.set<UiClickable>({d.action});
    }
    if ((b & kBhDrag) != 0) {
        UiDraggable drag{};
        drag.value = d.value;
        drag.minV = d.minV;
        drag.maxV = d.maxV;
        drag.axis = d.axis;
        drag.mode = d.mode;
        drag.flat = d.flat ? 1 : 0;
        e.set<UiDraggable>(drag);
        e.set<UiDragState>({});
    }
    if ((b & (kBhScroll | kBhVirtual)) != 0) {
        e.add<UiScrollable>();
        e.add<UiClipChildren>();
        e.set<UiScroll>({});
    }
    if ((b & kBhVirtual) != 0) {
        UiVirtual v{};
        v.count = d.count;
        v.itemW = d.itemW;
        v.itemH = d.itemH;
        v.columns = std::max(1u, d.columns);
        v.itemAction = d.itemAction;
        v.itemClass = itemClass;
        e.set<UiVirtual>(v);
    }
    if ((b & kBhEdit) != 0) {
        UiEditable edit{};
        edit.caret = d.textLen;
        edit.selAnchor = d.textLen;
        edit.maxLen = d.maxLen;
        edit.multiline = d.multiline ? 1 : 0;
        e.set<UiEditable>(edit);
    }
    if ((b & kBhOverlay) != 0) {
        UiOverlay o{};
        o.placement = d.placement;
        o.dismissOnOutside = d.dismiss;
        o.layer = d.layer;
        o.open = 0;
        e.set<UiOverlay>(o);
        e.add<UiRoot>();
        e.add<UiHidden>();
    }
    if (d.passive) {
        e.add<UiPassive>();
    }
    if (d.tooltip[0] != '\0') {
        UiTooltipText tip{};
        std::memcpy(tip.text, d.tooltip, sizeof(tip.text));
        e.set<UiTooltipText>(tip);
    }
    if (d.bind[0] != '\0') {
        UiBindKey bind{};
        std::memcpy(bind.key, d.bind, sizeof(bind.key));
        e.set<UiBindKey>(bind);
    }
    if (d.tooltipHost) {
        e.add<UiTooltipHost>();
    }
    if (d.hidden) {
        guiSetShown(e, false);
    }
}

bool parseNode(LoadCtx& ctx, json::Cur& c, flecs::entity parent, uint32_t depth);

bool parseChildren(LoadCtx& ctx, json::Cur c, flecs::entity parent, uint32_t depth);

// `{"$include": "path"}` in a children array: the file holds an array of nodes that take the object's place.
// Returns 0 when `c` is not an include, 1 when it was spliced in, -1 on failure.
int parseInclude(LoadCtx& ctx, json::Cur& c, flecs::entity parent, uint32_t depth) {
    json::Cur probe = c;
    char key[24] = {};
    if (!json::eat(probe, '{') || !readKey(probe, key, sizeof(key)) || !isKey(key, "$include")) {
        return 0;
    }
    char path[kGuiIncludePathCap] = {};
    uint8_t len = 0;
    if (!json::eat(probe, ':') || !readString(probe, path, sizeof(path), len) || !json::eat(probe, '}') ||
        ctx.includeDepth >= kGuiIncludeDepthCap) {
        return -1;
    }
    c = probe;
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        return -1;
    }
    ++ctx.includeDepth;
    const json::Cur body{reinterpret_cast<const char*>(file.data), reinterpret_cast<const char*>(file.data) + file.size};
    const bool ok = parseChildren(ctx, body, parent, depth);
    --ctx.includeDepth;
    fileUnmap(&file);
    return ok ? 1 : -1;
}

bool parseChildren(LoadCtx& ctx, json::Cur c, flecs::entity parent, uint32_t depth) {
    if (!json::eat(c, '[')) {
        return false;
    }
    if (json::eat(c, ']')) {
        return true;
    }
    for (;;) {
        const int included = parseInclude(ctx, c, parent, depth);
        if (included < 0 || (included == 0 && !parseNode(ctx, c, parent, depth + 1))) {
            return false;
        }
        if (json::eat(c, ']')) {
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

flecs::entity buildEntity(LoadCtx& ctx, const NodeDesc& d, flecs::entity parent) {
    flecs::world& world = *ctx.world;
    const bool top = !parent.is_valid();
    flecs::entity e = top ? world.entity() : world.entity().child_of(parent);
    if (!ctx.root.is_valid()) {
        ctx.root = e;
    }
    e.add(flecs::OrderedChildren);
    if (top || (d.behaviors & kBhOverlay) != 0) {
        e.add<UiRoot>();
    }
    if (d.name[0] != '\0') {
        const flecs::entity clash = top ? world.lookup(d.name) : parent.lookup(d.name);
        if (!clash.is_valid()) {
            e.set_name(d.name);
        }
    }
    UiBox box{};
    UiPaint paint{};
    UiText text{};
    std::memcpy(text.text, d.text, d.textLen);
    text.len = d.textLen;
    e.set<UiBox>(box);
    e.set<UiPaint>(paint);
    e.set<UiText>(text);
    if (!guiLayoutAttach(e)) {
        return e;
    }
    if (!top && (d.behaviors & kBhOverlay) != 0) {
        // An overlay is its own layout root: it stays a flecs child (for name lookup) but leaves the parent's Yoga tree.
        const UiFlex* parentFlex = parent.try_get<UiFlex>();
        const UiFlex* selfFlex = e.try_get<UiFlex>();
        if (parentFlex != nullptr && parentFlex->node != nullptr && selfFlex != nullptr && selfFlex->node != nullptr) {
            YGNodeRemoveChild(parentFlex->node, selfFlex->node);
        }
    }
    const uint32_t classId = d.cls[0] != '\0' ? guiStyleFind(*ctx.sheet, d.cls) : kGuiNoClass;
    if (classId != kGuiNoClass) {
        guiStyleApply(*ctx.sheet, e, classId);
    }
    // Explicit keys win over the class.
    UiPaint* p = e.try_get_mut<UiPaint>();
    UiText* t = e.try_get_mut<UiText>();
    if (p != nullptr) {
        if (d.hasColor) {
            p->token = 0;
            std::copy_n(d.color, 4, p->tint);
        } else if (p->token == 0) {
            p->tint[3] = 0.0f;
        }
        if (d.photo >= 0) {
            p->photo = static_cast<uint8_t>(d.photo);
        }
        if (d.icon != 0) {
            p->icon = d.icon;
            if (d.textLen != 0) { // a caption with an icon: the glyph trails the text, so reserve its room
                if (const UiFlex* flex = e.try_get<UiFlex>(); flex != nullptr && flex->node != nullptr) {
                    const float room = kGuiIconDrawPx + kGuiIconTrailPad;
                    YGNodeStyleSetPadding(flex->node, YGEdgeRight, YGNodeStyleGetPadding(flex->node, YGEdgeRight).value + room);
                }
            }
        }
    }
    if (t != nullptr) {
        if (d.size > 0) {
            t->size = static_cast<uint8_t>(d.size);
        }
        if (d.align >= 0) {
            t->align = static_cast<uint8_t>(d.align);
        }
    }
    applyBehaviors(e, d, d.itemClass[0] != '\0' ? guiStyleFind(*ctx.sheet, d.itemClass) : kGuiNoClass);
    if (d.toggles[0] != '\0' && ctx.toggleCount < kGuiToggleCap) {
        PendingToggle& pending = ctx.toggles[ctx.toggleCount++];
        pending.entity = e.id();
        std::memcpy(pending.path, d.toggles, sizeof(pending.path));
    }
    return e;
}

bool parseNode(LoadCtx& ctx, json::Cur& c, flecs::entity parent, uint32_t depth) {
    if (depth >= kGuiLoadDepthCap || !json::eat(c, '{')) {
        return false;
    }
    NodeDesc d{};
    json::skip(c);
    if (!json::eat(c, '}')) {
        for (;;) {
            char key[24] = {};
            if (!readKey(c, key, sizeof(key)) || !json::eat(c, ':') || !readField(c, key, d)) {
                return false;
            }
            if (json::eat(c, '}')) {
                break;
            }
            if (!json::eat(c, ',')) {
                return false;
            }
        }
    }
    const flecs::entity e = buildEntity(ctx, d, parent);
    if (!e.has<UiFlex>()) {
        return false;
    }
    return !d.hasChildren || parseChildren(ctx, d.children, e, depth);
}

} // namespace

bool guiLoadScreen(flecs::world& world, const GuiStyleSheet& sheet, std::string_view source, flecs::entity parent,
                   flecs::entity* rootOut) {
    if (rootOut != nullptr) {
        *rootOut = flecs::entity();
    }
    LoadCtx ctx;
    ctx.world = &world;
    ctx.sheet = &sheet;
    json::Cur cur{source.data(), source.data() + source.size()};
    bool ok = parseNode(ctx, cur, parent, 0);
    for (uint32_t i = 0; ok && i < ctx.toggleCount; ++i) {
        const flecs::entity target = ctx.root.lookup(ctx.toggles[i].path);
        ok = target.is_valid();
        if (ok) {
            world.entity(static_cast<flecs::entity_t>(ctx.toggles[i].entity)).set<UiToggle>({target.id()});
        }
    }
    if (!ok) {
        if (ctx.root.is_valid() && ctx.root.is_alive()) {
            guiLayoutDetach(ctx.root);
            ctx.root.destruct();
        }
        return false;
    }
    if (rootOut != nullptr) {
        *rootOut = ctx.root;
    }
    return true;
}

} // namespace burnhope::gui
