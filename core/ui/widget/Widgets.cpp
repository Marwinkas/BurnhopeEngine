#include "ui/Command.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Gradient.hpp"
#include "ui/widget/Widgets.hpp"
#include "ui/State.hpp"
#include "ui/UiBin.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Search.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/Theme.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

void uiWidgetsInit(UiState& s) {
    uiCommandBind(s);
    uiBinBuiltin(s);
    uiButtonBin(s);
    uiCheckBin(s);
    uiLabelBin(s);
    uiSliderBin(s);
    uiFieldBin(s);
    uiSearchBin(s);
    uiScrollBin(s);
    uiMenuBin(s);
    uiChoiceBind(s);
    uiImageBin(s);
    uiSplitBin(s);
    uiTreeBin(s);
    uiGridBin(s);
    uiCrumbsBin(s);
    uiInstallChromeBin(s);
    uiInstallDockOverlay(s);
    uiNumberWatch(s);
    uiPlaceAdd(s, uiScrollPlace);
    // Earlier input hook eats the gesture. This list is the protocol.
    uiInputAdd(s, uiSplitInput);
    uiInputAdd(s, uiNumberInput);
    uiInputAdd(s, uiFieldInput);
    uiInputAdd(s, uiRubberInput);
    uiInputAdd(s, uiMenuInput);
    uiInputAdd(s, uiDockInput);
    uiInputAdd(s, uiScrollInput);
    uiInputAdd(s, uiVirtualGridInput);
    uiInputAdd(s, uiTreeInput);
    uiInputAdd(s, uiDragDropInput);
    uiInputAdd(s, uiSliderInput);
    uiInstallColorBin(s);
    uiInstallCalendarBin(s);
    uiCurveBind(s);
    uiGradientBind(s);
    // Widgets spawn blueprints that read the sheet, so a state starts with the shipped theme, sheet and blueprints.
    // A host reloads all three on F5 (canvasReload).
    if (!uiThemeLoadAsset(s, "assets/ui/theme.json")) {
        spdlog::warn("ui: assets/ui/theme.json unavailable, themes fall back to the default palette");
    }
    if (!uiStyleLoadAsset(s, "assets/ui/styles.json")) {
        spdlog::warn("ui: assets/ui/styles.json unavailable, widgets use the built-in style classes");
    }
    if (!uiTemplatesLoadAsset(s, "assets/ui/core/_manifest.json")) {
        spdlog::warn("ui: assets/ui/core blueprints unavailable, buttons cannot be built");
    }
}

} // namespace burnhope
