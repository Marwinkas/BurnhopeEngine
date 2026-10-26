#pragma once

#include "ui/StyleSheet.hpp"

#include <cstdint>
#include <string_view>

namespace burnhope::json {
struct Cur;
}

namespace burnhope::style {

// FNV-1a over the class name; UiStyleSheet keeps its classes sorted by it.
[[nodiscard]] uint32_t nameHash(const char* name);
[[nodiscard]] const UiStyleClass* findIn(const UiStyleSheet& sheet, const char* name);
[[nodiscard]] UiStyleClass* findMutIn(UiStyleSheet& sheet, const char* name);

// "tokens" of a style document: named numbers, "group.name" for the nested ones.
struct Constants {
    char name[kUiStyleTokenCap][24]{};
    float value[kUiStyleTokenCap]{};
    uint8_t count = 0;
};

[[nodiscard]] bool parseTokens(json::Cur& c, Constants& out);
// A number, or "$name" / "$group.name" naming a token.
[[nodiscard]] bool parseValue(json::Cur& c, const Constants& tokens, const char* cls, const char* key, float& out);

// Parses a whole style document into `out`: empty for a fresh load, or holding the live sheet with `overlay` set,
// which merges a class the document repeats into the one already there. Errors are logged with the class and key.
[[nodiscard]] bool parseSheet(std::string_view text, UiStyleSheet& out, bool overlay);

} // namespace burnhope::style
