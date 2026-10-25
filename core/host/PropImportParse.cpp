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

} // namespace burnhope::propimport
