#include "host/PropImportInternal.hpp"
#include "Str.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope::propimport {

bool fail(Import& in, const char* at, const char* message, const char* detail) {
    if (in.failed) {
        return false;
    }
    in.failed = true;
    uint32_t line = 1;
    uint32_t col = 1;
    if (in.text != nullptr && at != nullptr && at >= in.text) {
        for (const char* p = in.text; p < at; ++p) {
            if (*p == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
    }
    spdlog::error("{}:{}:{}: {} {}", in.asset != nullptr ? in.asset : "layout", line, col, message, detail);
    return false;
}

bool parseItem(Import& in, Cur& c, Item& item) {
    if (!json::eat(c, '{')) {
        return fail(in, c.p, "expected an item object");
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
            return fail(in, keyAt, "expected an item key");
        }
        bool ok = false;
        if (std::strcmp(key, "label") == 0) {
            ok = json::parseStringCap(c, item.label, sizeof(item.label), n);
        } else if (std::strcmp(key, "command") == 0) {
            ok = json::parseStringCap(c, item.command, sizeof(item.command), n);
        } else if (std::strcmp(key, "value") == 0) {
            double number = 0.0;
            ok = json::parseNumber(c, number);
            item.value = static_cast<uint8_t>(number);
            item.hasValue = true;
        } else {
            return fail(in, keyAt, "unknown item key", key);
        }
        if (!ok) {
            return fail(in, keyAt, "bad value for item key", key);
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return fail(in, c.p, "expected , or } in an item");
        }
    }
}

bool parseShow(Import& in, Cur& c, Node& node) {
    if (!json::eat(c, '{')) {
        return fail(in, c.p, "showWhen must be an object");
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        const char* keyAt = c.p;
        char key[16]{};
        uint32_t n = 0;
        double number = 0.0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':') || !json::parseNumber(c, number)) {
            return fail(in, keyAt, "showWhen takes flag, eq or ne numbers");
        }
        if (std::strcmp(key, "flag") == 0) {
            node.showFlag = static_cast<uint16_t>(number);
        } else if (std::strcmp(key, "eq") == 0 || std::strcmp(key, "ne") == 0) {
            node.showValue = static_cast<uint8_t>(number);
            node.showOp = key[0] == 'n' ? 1 : 0;
        } else {
            return fail(in, keyAt, "unknown showWhen key", key);
        }
        node.hasShow = true;
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, '}') || fail(in, c.p, "unterminated showWhen");
}

bool parseItems(Import& in, Cur& c, Node& node) {
    if (!json::eat(c, '[')) {
        return fail(in, c.p, "items must be an array");
    }
    json::skip(c);
    while (c.p < c.end && *c.p != ']') {
        if (node.itemN >= 8) {
            return fail(in, c.p, "a node holds at most 8 items");
        }
        if (!parseItem(in, c, node.items[node.itemN])) {
            return false;
        }
        ++node.itemN;
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, ']') || fail(in, c.p, "unterminated items");
}

bool applyClass(Import& in, const char* at, Node& node, const char* name) {
    UiStyleClass cls;
    if (!uiStyleResolve(*in.state, name, cls)) {
        return fail(in, at, "unknown style class", name);
    }
    // Explicit node attributes win over the class: only fields still at their default take the class value.
    const Node blank{};
    auto take = [&](UiStyleKey key, float& dst, float untouched) {
        if (dst == untouched && uiStyleHas(&cls, key)) {
            dst = cls.value[static_cast<size_t>(key)];
        }
    };
    // The "window_panel" class is the popup look (uiStyleLoad feeds it to paintPopup), which the "veil" paint draws.
    if (std::strcmp(name, "window_panel") == 0 && node.paintName[0] == '\0') {
        strCopyCap(node.paintName, sizeof(node.paintName), "veil");
    }
    take(UiStyleKey::Height, node.h, blank.h);
    take(UiStyleKey::Width, node.w, blank.w);
    take(UiStyleKey::MinW, node.minW, blank.minW);
    take(UiStyleKey::Pad, node.pad, blank.pad);
    take(UiStyleKey::PadL, node.padL, blank.padL);
    take(UiStyleKey::PadR, node.padR, blank.padR);
    take(UiStyleKey::Gap, node.gap, blank.gap);
    return true;
}

} // namespace burnhope::propimport
