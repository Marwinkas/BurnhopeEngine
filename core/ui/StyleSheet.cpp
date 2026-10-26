#include "ui/StyleSheet.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {
namespace {

struct KeyName {
    const char* name;
    UiStyleKey key;
};

constexpr KeyName kKeys[] = {
    {"h", UiStyleKey::Height}, {"inner", UiStyleKey::Inner}, {"w", UiStyleKey::Width}, {"maxW", UiStyleKey::MaxW},
    {"minW", UiStyleKey::MinW}, {"pad", UiStyleKey::Pad}, {"padL", UiStyleKey::PadL}, {"padR", UiStyleKey::PadR},
    {"gap", UiStyleKey::Gap}, {"border", UiStyleKey::Border}, {"radius", UiStyleKey::Radius},
    {"icon", UiStyleKey::Icon}, {"alpha", UiStyleKey::Alpha}, {"trail", UiStyleKey::Trail},
};

struct TokenName {
    const char* name;
    UiToken token;
};

constexpr TokenName kTokens[] = {
    {"window", UiToken::Window}, {"title", UiToken::Title}, {"side", UiToken::Side}, {"content", UiToken::Content},
    {"button", UiToken::Button}, {"field", UiToken::Field}, {"menu", UiToken::Menu}, {"text", UiToken::Text},
    {"muted", UiToken::Muted}, {"item", UiToken::Item}, {"accent", UiToken::Accent},
};

uint16_t bit(UiStyleKey key) {
    return static_cast<uint16_t>(1u << static_cast<uint8_t>(key));
}

bool parseClass(json::Cur& c, UiStyleClass& out) {
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
        if (std::strcmp(key, "bg") == 0 || std::strcmp(key, "edge") == 0) {
            char text[16]{};
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            UiToken token = UiToken::Custom;
            for (const TokenName& entry : kTokens) {
                if (std::strcmp(entry.name, text) == 0) {
                    token = entry.token;
                }
            }
            (key[0] == 'b' ? out.bg : out.edge) = token;
        } else {
            bool known = false;
            for (const KeyName& entry : kKeys) {
                if (std::strcmp(entry.name, key) != 0) {
                    continue;
                }
                double number = 0.0;
                if (!json::parseNumber(c, number)) {
                    return false;
                }
                out.value[static_cast<size_t>(entry.key)] = static_cast<float>(number);
                out.has = static_cast<uint16_t>(out.has | bit(entry.key));
                known = true;
                break;
            }
            if (!known && !json::skipValue(c)) {
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

bool parseClasses(json::Cur& c, UiStyleSheet& sheet) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        UiStyleClass entry{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, entry.name, sizeof(entry.name), n) || !json::eat(c, ':') || !parseClass(c, entry)) {
            return false;
        }
        if (sheet.count < kUiStyleCap) {
            sheet.cls[sheet.count++] = entry;
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

bool parseSheet(std::string_view text, UiStyleSheet& sheet) {
    json::Cur c{text.data(), text.data() + text.size()};
    if (!json::eat(c, '{')) {
        return false;
    }
    for (;;) {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        if (std::strcmp(key, "classes") == 0) {
            if (!parseClasses(c, sheet)) {
                return false;
            }
        } else if (!json::skipValue(c)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

} // namespace

bool uiStyleLoad(UiState& s, std::string_view json) {
    UiStyleSheet sheet{};
    if (!parseSheet(json, sheet)) {
        return false;
    }
    s.world.ensure<UiStyleSheet>() = sheet;
    const UiStyleClass* panel = uiStyleFind(s, "window_panel");
    if (panel != nullptr) {
        s.look.popupAlpha = uiStyle(s, "window_panel", UiStyleKey::Alpha, s.look.popupAlpha);
        s.look.popupBorder = uiStyle(s, "window_panel", UiStyleKey::Border, s.look.popupBorder);
        s.look.popupRadius = uiStyle(s, "window_panel", UiStyleKey::Radius, s.look.popupRadius);
    }
    return true;
}

bool uiStyleLoadAsset(UiState& s, const char* asset) {
    char path[512]{};
    if (asset == nullptr || !platformResolveAsset(asset, path, sizeof(path))) {
        spdlog::error("style sheet {} was not found", asset != nullptr ? asset : "");
        return false;
    }
    MappedFile file{};
    if (!fileMapReadOnly(path, &file) || file.data == nullptr) {
        spdlog::error("style sheet {} did not map", path);
        return false;
    }
    const bool ok = uiStyleLoad(s, {reinterpret_cast<const char*>(file.data), file.size});
    fileUnmap(&file);
    if (!ok) {
        spdlog::error("style sheet {} did not parse", path);
    }
    return ok;
}

const UiStyleClass* uiStyleFind(const UiState& s, const char* name) {
    const UiStyleSheet* sheet = s.world.try_get<UiStyleSheet>();
    if (sheet == nullptr || name == nullptr) {
        return nullptr;
    }
    for (uint8_t i = 0; i < sheet->count; ++i) {
        if (std::strcmp(sheet->cls[i].name, name) == 0) {
            return &sheet->cls[i];
        }
    }
    return nullptr;
}

bool uiStyleHas(const UiStyleClass* cls, UiStyleKey key) {
    return cls != nullptr && (cls->has & bit(key)) != 0;
}

float uiStyle(const UiState& s, const char* cls, UiStyleKey key, float fallback) {
    const UiStyleClass* found = uiStyleFind(s, cls);
    return uiStyleHas(found, key) ? found->value[static_cast<size_t>(key)] : fallback;
}

} // namespace burnhope
