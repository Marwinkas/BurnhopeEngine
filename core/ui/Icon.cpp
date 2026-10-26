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

} // namespace burnhope
