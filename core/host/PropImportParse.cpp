#include "host/PropImportInternal.hpp"

#include <cstring>

namespace burnhope::propimport {

bool keyIs(const char* key, const char* name) {
    return std::strcmp(key, name) == 0;
}

bool parseItem(Cur& c, Item& item) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        if (keyIs(key, "label")) {
            if (!json::parseStringCap(c, item.label, sizeof(item.label), n)) {
                return false;
            }
        } else if (keyIs(key, "command")) {
            if (!json::parseStringCap(c, item.command, sizeof(item.command), n)) {
                return false;
            }
        } else if (keyIs(key, "value")) {
            double number = 0.0;
            if (!json::parseNumber(c, number)) {
                return false;
            }
            item.value = static_cast<uint8_t>(number);
            item.hasValue = true;
        } else if (!json::skipValue(c)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

bool parseShow(Cur& c, Node& node) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        double number = 0.0;
        if (!json::parseNumber(c, number)) {
            return false;
        }
        if (keyIs(key, "flag")) {
            node.showFlag = static_cast<uint16_t>(number);
            node.hasShow = true;
        } else if (keyIs(key, "eq")) {
            node.showValue = static_cast<uint8_t>(number);
            node.showOp = 0;
            node.hasShow = true;
        } else if (keyIs(key, "ne")) {
            node.showValue = static_cast<uint8_t>(number);
            node.showOp = 1;
            node.hasShow = true;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

bool parseItems(Cur& c, Node& node) {
    if (!json::eat(c, '[')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == ']') {
        ++c.p;
        return true;
    }
    for (;;) {
        if (node.itemN >= 8) {
            return false;
        }
        if (!parseItem(c, node.items[node.itemN])) {
            return false;
        }
        ++node.itemN;
        json::skip(c);
        if (c.p < c.end && *c.p == ']') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

void applyClass(const UiState& s, Node& node, const char* name) {
    const UiStyleClass* cls = uiStyleFind(s, name);
    if (cls == nullptr) {
        return;
    }
    // Explicit node attributes win over the class regardless of key order: only fields still at their default take the class value.
    const Node blank{};
    auto take = [&](UiStyleKey key, float& dst, float untouched) {
        if (dst == untouched && uiStyleHas(cls, key)) {
            dst = cls->value[static_cast<size_t>(key)];
        }
    };
    take(UiStyleKey::Height, node.h, blank.h);
    take(UiStyleKey::Width, node.w, blank.w);
    take(UiStyleKey::MinW, node.minW, blank.minW);
    take(UiStyleKey::Pad, node.pad, blank.pad);
    take(UiStyleKey::PadL, node.padL, blank.padL);
    take(UiStyleKey::PadR, node.padR, blank.padR);
    take(UiStyleKey::Gap, node.gap, blank.gap);
}

} // namespace burnhope::propimport

namespace burnhope::propimport {

bool parseExtra(Cur& c, const char* key, Node& node) {
    if (keyIs(key, "slot") || keyIs(key, "channel") || keyIs(key, "cell")) {
        double number = 0.0;
        if (!json::parseNumber(c, number)) {
            return false;
        }
        const auto value = static_cast<uint8_t>(number);
        if (keyIs(key, "slot")) {
            node.slotN = value;
        } else if (keyIs(key, "channel")) {
            node.channel = value;
        } else {
            node.cell = value;
        }
        return true;
    }
    if (keyIs(key, "reset")) {
        json::skip(c);
        if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
            node.reset = true;
            c.p += 4;
            return true;
        }
        if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
            node.reset = false;
            c.p += 5;
            return true;
        }
        return false;
    }
    return json::skipValue(c);
}

} // namespace burnhope::propimport
