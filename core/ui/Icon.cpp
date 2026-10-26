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

const char* uiIconAssetPath(UiIcon icon) {
    switch (icon) {
        case UiIcon::Minimize: return "assets/icons/minimize.svg";
        case UiIcon::Maximize: return "assets/icons/maximize.svg";
        case UiIcon::Dismiss: return "assets/icons/dismiss.svg";
        case UiIcon::Folder: return "assets/icons/folder.svg";
        case UiIcon::Gear:
        case UiIcon::Cogwheel: return "assets/icons/gear.svg";
        case UiIcon::Menu: return "assets/icons/menu.svg";
        case UiIcon::Search: return "assets/icons/search.svg";
        case UiIcon::ChevronRight: return "assets/icons/chevron_right.svg";
        case UiIcon::MenuChevron:
        case UiIcon::ChevronDown: return "assets/icons/chevron_down.svg";
        case UiIcon::ChevronLeft: return "assets/icons/chevron_left.svg";
        case UiIcon::LockOpen: return "assets/icons/lock_open.svg";
        case UiIcon::Lock: return "assets/icons/lock.svg";
        case UiIcon::Reset: return "assets/icons/reset.svg";
        case UiIcon::Crosshair: return "assets/icons/crosshair.svg";
        case UiIcon::Add: return "assets/icons/plus.svg";
        case UiIcon::Play: return "assets/icons/play.svg";
        case UiIcon::Pause: return "assets/icons/pause.svg";
        case UiIcon::Stop: return "assets/icons/stop.svg";
        case UiIcon::Check:
        case UiIcon::None: break;
    }
    return nullptr;
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
