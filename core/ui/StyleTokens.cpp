#include "ui/StyleInternal.hpp"

#include "io/JsonCursor.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope::style {

namespace {
bool constantFind(const Constants& table, const char* name, float& out) {
    for (uint8_t i = 0; i < table.count; ++i) {
        if (std::strcmp(table.name[i], name) == 0) {
            out = table.value[i];
            return true;
        }
    }
    return false;
}

bool constantAdd(Constants& table, const char* group, const char* key, float value) {
    char full[48];
    const int n = group != nullptr ? std::snprintf(full, sizeof(full), "%s.%s", group, key) : std::snprintf(full, sizeof(full), "%s", key);
    if (n <= 0 || n >= static_cast<int>(sizeof(table.name[0])) || table.count >= kUiStyleTokenCap) {
        spdlog::error("style token {} is too long or the table is full", full);
        return false;
    }
    std::memcpy(table.name[table.count], full, static_cast<size_t>(n) + 1u);
    table.value[table.count++] = value;
    return true;
}

} // namespace

// {"group": {"name": 1}, "name": 2} -> "group.name", "name".
bool parseTokens(json::Cur& c, Constants& out) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        char key[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '{') {
            ++c.p;
            json::skip(c);
            while (c.p < c.end && *c.p != '}') {
                char inner[24]{};
                double number = 0.0;
                if (!json::parseStringCap(c, inner, sizeof(inner), n) || !json::eat(c, ':') || !json::parseNumber(c, number) ||
                    !constantAdd(out, key, inner, static_cast<float>(number))) {
                    return false;
                }
                json::skip(c);
                if (c.p < c.end && *c.p == ',') {
                    ++c.p;
                    json::skip(c);
                }
            }
            if (!json::eat(c, '}')) {
                return false;
            }
        } else {
            double number = 0.0;
            if (!json::parseNumber(c, number) || !constantAdd(out, nullptr, key, static_cast<float>(number))) {
                return false;
            }
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

// A number, or "$name" / "$group.name" naming a token.
bool parseValue(json::Cur& c, const Constants& tokens, const char* cls, const char* key, float& out) {
    json::skip(c);
    if (c.p < c.end && *c.p == '"') {
        char ref[32]{};
        uint32_t n = 0;
        if (json::parseStringCap(c, ref, sizeof(ref), n) && std::strcmp(ref, "$theme.radius") == 0) {
            out = kUiRadiusTheme;
            return true;
        }
        if (n > 0 && std::strcmp(ref, "$theme.radiusSm") == 0) {
            out = kUiRadiusThemeSm;
            return true;
        }
        if (ref[0] != '$' || !constantFind(tokens, ref + 1, out)) {
            spdlog::error("style class {} key {}: {} is not a number or a known $token", cls, key, ref);
            return false;
        }
        return true;
    }
    double number = 0.0;
    if (!json::parseNumber(c, number)) {
        spdlog::error("style class {} key {} is not a number", cls, key);
        return false;
    }
    out = static_cast<float>(number);
    return true;
}

} // namespace burnhope::style
