#include "ui/StyleInternal.hpp"

#include "io/JsonCursor.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace burnhope::style {
namespace {

struct TokenName {
    const char* name;
    UiToken token;
};

constexpr TokenName kTokens[] = {
    {"window", UiToken::Window}, {"dock", UiToken::Content}, {"title", UiToken::Title}, {"side", UiToken::Side}, {"content", UiToken::Content},
    {"button", UiToken::Button}, {"field", UiToken::Field}, {"menu", UiToken::Menu}, {"text", UiToken::Text},
    {"close", UiToken::Close}, {"preview", UiToken::Preview}, {"chrome", UiToken::Chrome}, {"slider", UiToken::Slider}, {"check", UiToken::Check}, {"muted", UiToken::Muted}, {"item", UiToken::Item}, {"accent", UiToken::Accent},
    {"accent1", UiToken::Accent1}, {"accent2", UiToken::Accent2}, {"toolActive", UiToken::ToolActive},
    {"axisX", UiToken::AxisX}, {"axisY", UiToken::AxisY}, {"axisZ", UiToken::AxisZ},
};

UiToken tokenNamed(const char* text) {
    for (const TokenName& entry : kTokens) {
        if (std::strcmp(entry.name, text) == 0) {
            return entry.token;
        }
    }
    return UiToken::Custom;
}

void overlay(UiStyleClass& dst, const UiStyleClass& src) {
    for (size_t i = 0; i < static_cast<size_t>(UiStyleKey::Count); ++i) {
        if (src.has.test(static_cast<UiStyleKey>(i))) {
            dst.value[i] = src.value[i];
        }
    }
    dst.has.merge(src.has);
    dst.bg = src.bg != UiToken::Custom ? src.bg : dst.bg;
    dst.activeBg = src.activeBg != UiToken::Custom ? src.activeBg : dst.activeBg;
    dst.borderBg = src.borderBg != UiToken::Custom ? src.borderBg : dst.borderBg;
    for (size_t i = 0; i < static_cast<size_t>(UiStyleTone::Count); ++i) {
        dst.tone[i] = src.tone[i] != UiToken::Custom ? src.tone[i] : dst.tone[i];
    }
}

// The tone whose JSON name is `key`, or Count.
size_t toneIndex(const char* key) {
    size_t index = 0;
    while (index < static_cast<size_t>(UiStyleTone::Count) && std::strcmp(uiStyleToneName(static_cast<UiStyleTone>(index)), key) != 0) {
        ++index;
    }
    return index;
}

bool parseClass(json::Cur& c, const Constants& tokens, const UiStyleSheet& sheet, UiStyleClass& cls) {
    if (!json::eat(c, '{')) {
        return false;
    }
    UiStyleClass own{};
    char base[24]{};
    json::skip(c);
    bool more = c.p < c.end && *c.p != '}';
    while (more) {
        char key[16]{};
        char text[24]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        if (std::strcmp(key, "bg") == 0) {
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            own.bg = tokenNamed(text);
        } else if (std::strcmp(key, "activeBg") == 0) {
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            own.activeBg = tokenNamed(text);
        } else if (std::strcmp(key, "borderBg") == 0) {
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            own.borderBg = tokenNamed(text);
        } else if (toneIndex(key) < static_cast<size_t>(UiStyleTone::Count)) {
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            own.tone[toneIndex(key)] = tokenNamed(text);
        } else if (std::strcmp(key, "extends") == 0) {
            if (!json::parseStringCap(c, base, sizeof(base), n)) {
                return false;
            }
        } else {
            size_t index = 0;
            while (index < static_cast<size_t>(UiStyleKey::Count) && std::strcmp(uiStyleKeyName(static_cast<UiStyleKey>(index)), key) != 0) {
                ++index;
            }
            if (index == static_cast<size_t>(UiStyleKey::Count)) {
                spdlog::warn("style class {}: unknown key {}", cls.name, key);
                if (!json::skipValue(c)) {
                    return false;
                }
            } else {
                float value = 0.0f;
                if (!parseValue(c, tokens, cls.name, key, value)) {
                    return false;
                }
                own.value[index] = value;
                own.has.set(static_cast<UiStyleKey>(index));
            }
        }
        json::skip(c);
        more = c.p < c.end && *c.p == ',';
        if (more) {
            ++c.p;
        }
    }
    if (!json::eat(c, '}')) {
        return false;
    }
    if (base[0] != '\0') {
        const UiStyleClass* parent = findIn(sheet, base);
        if (parent == nullptr) {
            spdlog::error("style class {} extends {}, which is not defined before it", cls.name, base);
            return false;
        }
        const UiStyleClass keep = cls;
        cls = *parent;
        std::memcpy(cls.name, keep.name, sizeof(cls.name));
        cls.hash = keep.hash;
    }
    overlay(cls, own);
    return true;
}

bool parseClasses(json::Cur& c, const Constants& tokens, UiStyleSheet& sheet, bool overlayMode) {
    if (!json::eat(c, '{')) {
        return false;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        UiStyleClass entry{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, entry.name, sizeof(entry.name), n) || !json::eat(c, ':')) {
            return false;
        }
        entry.hash = nameHash(entry.name);
        UiStyleClass* existing = findMutIn(sheet, entry.name);
        if (existing != nullptr && !overlayMode) {
            spdlog::error("style class {} is a duplicate", entry.name);
            return false;
        }
        if (existing == nullptr && sheet.count >= kUiStyleCap) {
            spdlog::error("style class {} is past the {} class cap", entry.name, static_cast<unsigned>(kUiStyleCap));
            return false;
        }
        if (!parseClass(c, tokens, sheet, entry)) {
            return false;
        }
        if (existing != nullptr) {
            // Overlay: the class keeps every key the document leaves out.
            overlay(*existing, entry);
        } else {
            sheet.cls[sheet.count++] = entry;
        }
        // findIn searches the hash-sorted order, so keep the partial table sorted as classes arrive.
        std::sort(sheet.cls, sheet.cls + sheet.count, [](const UiStyleClass& a, const UiStyleClass& b) {
            return a.hash != b.hash ? a.hash < b.hash : std::strcmp(a.name, b.name) < 0;
        });
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return json::eat(c, '}');
}

enum class Seek { Found, Absent, Malformed };

// Walks the top-level object once for `wanted`; skips every other member.
Seek seek(json::Cur& c, const char* wanted) {
    if (!json::eat(c, '{')) {
        return Seek::Malformed;
    }
    json::skip(c);
    while (c.p < c.end && *c.p != '}') {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return Seek::Malformed;
        }
        if (std::strcmp(key, wanted) == 0) {
            return Seek::Found;
        }
        if (!json::skipValue(c)) {
            return Seek::Malformed;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ',') {
            ++c.p;
            json::skip(c);
        }
    }
    return Seek::Absent;
}

} // namespace

uint32_t nameHash(const char* name) {
    uint32_t h = 2166136261u;
    for (; *name != '\0'; ++name) {
        h = (h ^ static_cast<uint8_t>(*name)) * 16777619u;
    }
    return h;
}

UiStyleClass* findMutIn(UiStyleSheet& sheet, const char* name) {
    const UiStyleClass* found = findIn(sheet, name);
    return found != nullptr ? sheet.cls + (found - sheet.cls) : nullptr;
}

const UiStyleClass* findIn(const UiStyleSheet& sheet, const char* name) {
    const uint32_t hash = nameHash(name);
    const UiStyleClass* end = sheet.cls + sheet.count;
    const UiStyleClass* it = std::lower_bound(sheet.cls, end, hash, [](const UiStyleClass& a, uint32_t h) { return a.hash < h; });
    for (; it != end && it->hash == hash; ++it) {
        if (std::strcmp(it->name, name) == 0) {
            return it;
        }
    }
    return nullptr;
}

bool parseSheet(std::string_view text, UiStyleSheet& out, bool overlayMode) {
    Constants tokens;
    json::Cur c{text.data(), text.data() + text.size()};
    json::Cur scan = c;
    const Seek hasTokens = seek(scan, "tokens");
    if (hasTokens == Seek::Malformed || (hasTokens == Seek::Found && !parseTokens(scan, tokens))) {
        return false;
    }
    scan = c;
    const Seek hasClasses = seek(scan, "classes");
    return hasClasses == Seek::Absent || (hasClasses == Seek::Found && parseClasses(scan, tokens, out, overlayMode));
}

} // namespace burnhope::style
