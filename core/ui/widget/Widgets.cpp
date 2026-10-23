#include "ui/widget/Button.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Widgets.hpp"
#include "ui/UiBin.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Gradient.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Field.hpp"
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

namespace burnhope {

void uiWidgetsInit(UiState& s) {
    uiBinBuiltin(s);
    uiButtonBin(s);
    uiLabelBin(s);
    uiSliderBin(s);
    uiFieldBin(s);
    uiScrollBin(s);
    uiMenuBin(s);
    uiImageBin(s);
    uiSplitBin(s);
    uiTreeBin(s);
    uiGridBin(s);
    uiCrumbsBin(s);
    uiInstallChromeBin(s);
    uiInstallDockOverlay(s);
    uiNumberWatch(s);
    uiDrawDecorAdd(s, uiNumberDecor);
    uiDrawDecorAdd(s, uiFieldDecor);
    uiDrawDecorAdd(s, uiCurveDecor);
    uiDrawDecorAdd(s, uiGradientDecor);
    uiDrawDecorAdd(s, uiSliderDecor);
    uiDrawDecorAdd(s, uiCheckDecor);
    uiOverlayAdd(s, uiScrollOverlay);
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
}

} // namespace burnhope
