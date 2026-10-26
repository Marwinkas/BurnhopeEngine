#include "ui/Icon.hpp"

namespace burnhope {

UiIcon uiIconFromName(std::string_view name) {
    struct Entry {
        std::string_view name;
        UiIcon icon;
    };
    static constexpr Entry kTable[] = {
#define BH_ICON(text, id) {text, UiIcon::id},
#include "ui/IconNames.inc"
#undef BH_ICON
    };
    for (const Entry& e : kTable) {
        if (e.name == name) {
            return e.icon;
        }
    }
    return UiIcon::None;
}

bool uiIconIsPath(std::string_view name) {
    if (name.empty() || uiIconFromName(name) != UiIcon::None) {
        return false;
    }
    if (name.find('/') != std::string_view::npos) {
        return true;
    }
    const auto endsWith = [&](std::string_view ext) noexcept {
        if (name.size() < ext.size()) {
            return false;
        }
        const std::string_view tail = name.substr(name.size() - ext.size());
        for (size_t i = 0; i < ext.size(); ++i) {
            const char ch = tail[i] >= 'A' && tail[i] <= 'Z' ? static_cast<char>(tail[i] + ('a' - 'A')) : tail[i];
            if (ch != ext[i]) {
                return false;
            }
        }
        return true;
    };
    return endsWith(".svg") || endsWith(".png");
}

} // namespace burnhope
