#include "ui/Theme.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope {

struct UiThemeSheet {
    char name[kUiThemeCap][16]{};
    UiLook look[kUiThemeCap]{};
    uint8_t count = 0;
};

namespace {

using json::Cur;

struct Triple {
    const char* key;
    float (UiLook::*field)[3];
};

struct Scalar {
    const char* key;
    float UiLook::*field;
};

constexpr Triple kTriples[] = {
    {"window", &UiLook::window}, {"title", &UiLook::title}, {"side", &UiLook::side}, {"content", &UiLook::content},
    {"button", &UiLook::button}, {"field", &UiLook::field}, {"menu", &UiLook::menu}, {"close", &UiLook::close},
    {"text", &UiLook::text}, {"muted", &UiLook::muted}, {"item", &UiLook::item}, {"preview", &UiLook::preview},
    {"status", &UiLook::status}, {"slider", &UiLook::slider}, {"check", &UiLook::check}, {"chrome", &UiLook::chrome},
    {"tool", &UiLook::tool}, {"toolHover", &UiLook::toolHover}, {"toolActive", &UiLook::toolActive},
};

constexpr Scalar kScalars[] = {
    {"radius", &UiLook::radius}, {"radiusSm", &UiLook::radiusSm}, {"motion", &UiLook::motion}, {"hover", &UiLook::hover},
};

bool parseTriple(Cur& c, float (&out)[3]) {
    if (!json::eat(c, '[')) {
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        double v = 0.0;
        if (!json::parseNumber(c, v)) {
            return false;
        }
        out[i] = static_cast<float>(v);
        if (i < 2 && !json::eat(c, ',')) {
            return false;
        }
    }
    return json::eat(c, ']');
}

bool parseTheme(Cur& c, const char* theme, UiLook& look) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        bool known = false;
        for (const Triple& entry : kTriples) {
            if (std::strcmp(key, entry.key) == 0) {
                known = true;
                if (!parseTriple(c, look.*entry.field)) {
                    spdlog::error("theme {}: {} is three numbers in brackets", theme, key);
                    return false;
                }
            }
        }
        for (int i = 0; i < 3 && !known; ++i) {
            char accent[16];
            std::snprintf(accent, sizeof(accent), "accent%d", i);
            if (std::strcmp(key, accent) == 0) {
                known = true;
                if (!parseTriple(c, look.accent[i])) {
                    spdlog::error("theme {}: {} is three numbers in brackets", theme, key);
                    return false;
                }
            }
        }
        for (const Scalar& entry : kScalars) {
            if (std::strcmp(key, entry.key) == 0) {
                double v = 0.0;
                known = true;
                if (!json::parseNumber(c, v)) {
                    spdlog::error("theme {}: {} is a number", theme, key);
                    return false;
                }
                look.*entry.field = static_cast<float>(v);
            }
        }
        if (!known) {
            spdlog::error("theme {}: unknown key {}", theme, key);
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, '}');
}

bool parseThemes(Cur& c, UiThemeSheet& sheet) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char name[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, name, sizeof(name), n) || !json::eat(c, ':')) {
            return false;
        }
        for (uint8_t i = 0; i < sheet.count; ++i) {
            if (std::strcmp(sheet.name[i], name) == 0) {
                spdlog::error("theme {} is defined twice", name);
                return false;
            }
        }
        if (sheet.count >= kUiThemeCap) {
            spdlog::error("theme {} is past the {} theme cap", name, static_cast<unsigned>(kUiThemeCap));
            return false;
        }
        UiLook look{};
        if (!parseTheme(c, name, look)) {
            return false;
        }
        std::strcpy(sheet.name[sheet.count], name);
        sheet.look[sheet.count] = look;
        ++sheet.count;
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, '}');
}

} // namespace

bool uiThemeLoad(UiState& s, std::string_view text) {
    UiThemeSheet sheet{};
    Cur c{text.data(), text.data() + text.size()};
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    bool found = false;
    while (c.p < c.end && *c.p != '}') {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        if (std::strcmp(key, "themes") == 0) {
            if (!parseThemes(c, sheet)) {
                return false;
            }
            found = true;
        } else if (!json::skipValue(c)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    if (!found || !json::eat(c, '}')) {
        return false;
    }
    s.world.ensure<UiThemeSheet>() = sheet;
    return true;
}

bool uiThemeLoadAsset(UiState& s, const char* asset) {
    char path[512]{};
    if (asset == nullptr || !platformResolveAsset(asset, path, sizeof(path))) {
        spdlog::error("theme {} was not found", asset != nullptr ? asset : "");
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        spdlog::error("theme {} did not map", path);
        return false;
    }
    const bool ok = uiThemeLoad(s, {reinterpret_cast<const char*>(file.data), file.size});
    fileUnmap(&file);
    if (!ok) {
        spdlog::error("theme {} did not parse", path);
    }
    return ok;
}

bool uiThemeLook(const UiState& s, const char* name, UiLook& out) {
    const UiThemeSheet* sheet = s.world.try_get<UiThemeSheet>();
    if (sheet == nullptr || name == nullptr) {
        return false;
    }
    for (uint8_t i = 0; i < sheet->count; ++i) {
        if (std::strcmp(sheet->name[i], name) == 0) {
            out = sheet->look[i];
            return true;
        }
    }
    return false;
}

} // namespace burnhope
