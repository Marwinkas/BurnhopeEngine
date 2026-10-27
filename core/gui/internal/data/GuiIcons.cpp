#include "gui/GuiIcons.hpp"

#include <iterator>

namespace burnhope::gui {
namespace {

struct IconDef {
    const char* name;
    const char* path;
};

// Index = atlas cell; entry 0 is the "none" slot.
constexpr IconDef kIcons[] = {
    {"", nullptr},
    {"select", "assets/icons/SelectMode.svg"},
    {"move", "assets/icons/MoveMode.svg"},
    {"rotate", "assets/icons/RotateMode.svg"},
    {"scale", "assets/icons/ScallingMode.svg"},
    {"snap", "assets/icons/Snap.svg"},
    {"maximize", "assets/icons/maximize.svg"},
    {"minimize", "assets/icons/minimize.svg"},
    {"play", "assets/icons/play.svg"},
    {"pause", "assets/icons/pause.svg"},
    {"stop", "assets/icons/stop.svg"},
    {"camera", "assets/icons/Camera.svg"},
    {"gear", "assets/icons/gear.svg"},
    {"settings", "assets/icons/settings.svg"},
    {"chevron_down", "assets/icons/chevron_down.svg"},
    {"chevron_up", "assets/icons/chevron_up.svg"},
    {"chevron_left", "assets/icons/chevron_left.svg"},
    {"chevron_right", "assets/icons/chevron_right.svg"},
    {"plus", "assets/icons/plus.svg"},
    {"square_plus", "assets/icons/square-plus.svg"},
    {"search", "assets/icons/search.svg"},
    {"folder", "assets/icons/folder.svg"},
    {"file", "assets/icons/file.svg"},
    {"file_image", "assets/icons/file-image.svg"},
    {"file_box", "assets/icons/file-box.svg"},
    {"file_braces", "assets/icons/file-braces.svg"},
    {"reset", "assets/icons/reset.svg"},
    {"check", "assets/icons/check.svg"},
    {"x", "assets/icons/x.svg"},
    {"dismiss", "assets/icons/dismiss.svg"},
    {"trash", "assets/icons/trash.svg"},
    {"list_filter", "assets/icons/list-filter.svg"},
    {"lock", "assets/icons/lock.svg"},
    {"lock_open", "assets/icons/lock_open.svg"},
    {"scene", "assets/icons/scene.svg"},
    {"project", "assets/icons/project.svg"},
    {"docs", "assets/icons/docs.svg"},
    {"upload", "assets/icons/UploadFile.svg"},
    {"download", "assets/icons/download.svg"},
    {"menu", "assets/icons/menu.svg"},
    {"ellipsis", "assets/icons/ellipsis.svg"},
    {"crosshair", "assets/icons/crosshair.svg"},
    {"box", "assets/icons/box.svg"},
};

static_assert(std::size(kIcons) <= kGuiIconCells * kGuiIconCells);

} // namespace

uint8_t guiIconFind(std::string_view name) {
    for (uint32_t i = 1; i < std::size(kIcons); ++i) {
        if (name == kIcons[i].name) {
            return static_cast<uint8_t>(i);
        }
    }
    return 0;
}

const char* guiIconPath(uint32_t cell) {
    return cell < std::size(kIcons) ? kIcons[cell].path : nullptr;
}

uint32_t guiIconCount() {
    return static_cast<uint32_t>(std::size(kIcons));
}

} // namespace burnhope::gui
