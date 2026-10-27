#pragma once

#include <cstdint>
#include <type_traits>

#include <flecs.h>
#include <yoga/Yoga.h>

namespace burnhope::gui {

constexpr uint32_t kGuiTextCap = 128;
constexpr uint32_t kGuiSplitterCap = 32; // splitter grips one screen can hold
constexpr uint32_t kGuiDockCap = 16;     // tabbed docks one screen can hold
constexpr uint32_t kGuiDockPanelCap = 16; // panels (and tabs) one dock can hold
constexpr uint8_t kGuiNoPhoto = 255;
constexpr uint32_t kGuiNoClass = 0xFFFFu;

// ---- Data components (trivially copyable POD, no methods) ----

// Yoga node owned by the layout engine. The entity is stored in YGNodeGetContext.
struct UiFlex {
    YGNodeRef node = nullptr;
};

// Absolute screen-space box produced by the layout engine (scroll offsets of ancestors already applied).
struct UiBox {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// Visible region (intersection of all ancestor scroll/popup clips and the node's own box) written by the scissor pass.
// w <= 0 or h <= 0 means fully clipped.
struct UiClip {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct UiPaint {
    uint8_t token = 0;
    uint8_t borderToken = 0;
    uint8_t photo = kGuiNoPhoto;
    uint8_t icon = 0; // atlas cell of GuiIcons.hpp drawn centred in the box, tinted by the text colour; 0 = none
    uint8_t cell = 0; // photo atlas cell (row-major in a cols x rows grid); only read when cols != 0
    uint8_t cols = 0; // 0 = the photo shows its whole slot
    uint8_t rows = 0;
    uint8_t tile = 0; // != 0: the photo is a tile x tile square at the top centre of the box (file tiles), drawn untinted
    float radius = 0.0f;
    float borderW = 0.0f;
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // multiplied into the token color; hover tints write here
};
static_assert(sizeof(UiPaint) % 16 == 0);

struct UiText {
    char text[kGuiTextCap] = {};
    uint8_t len = 0;
    uint8_t align = 0; // 0 left, 1 center, 2 right
    uint8_t size = 14; // px
    uint8_t pad0 = 0;
};

// Hint an empty, unfocused UiEditable shows in the muted colour.
struct UiPlaceholder {
    char text[32] = {};
};

// Style class assigned from styles.json (index into GuiStyleSheet).
struct UiClass {
    uint32_t id = kGuiNoClass;
};

// Scroll container state. Layout subtracts offX/offY from descendants' boxes.
struct UiScroll {
    float offX = 0.0f;
    float offY = 0.0f;
    float contentW = 0.0f;
    float contentH = 0.0f;
    float viewW = 0.0f;
    float viewH = 0.0f;
};

// Drag engine configuration + result. Axis: 0 horizontal, 1 vertical, 2 both (x drives value, y in valueY).
struct UiDraggable {
    float value = 0.0f;  // normalized 0..1 along the axis
    float valueY = 0.0f; // normalized 0..1 on the second axis (axis == 2)
    float minV = 0.0f;
    float maxV = 1.0f;
    uint8_t axis = 0;
    uint8_t mode = 0; // 0 absolute track (value follows pointer inside track box), 1 relative delta, 2 scrub (pointer.dx accumulation)
    uint8_t flat = 0; // 1: the emitter draws no value bar (number fields whose range is not a meaningful fraction)
    uint8_t pad1 = 0;
    float step = 0.0f; // mode 2: real units per pixel (0 = a fraction of the range, see kGuiScrubPerPixel)
};

// Runtime drag session, written only by InteractDrag.
struct UiDragState {
    float originX = 0.0f;
    float originY = 0.0f;
    float startValue = 0.0f;
    float startValueY = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    uint8_t active = 0;
    uint8_t latched = 0; // mode 2: the 3px deadzone has been crossed
    uint8_t pad0 = 0;
    uint8_t pad1 = 0;
};

// Click payload: action id fired on release over the same entity.
struct UiClickable {
    uint32_t action = 0;
};

// Text editing state for UiEditable entities (byte offsets into UiText::text).
struct UiEditable {
    uint8_t caret = 0;
    uint8_t selAnchor = 0;
    uint8_t maxLen = 127;
    uint8_t multiline = 0;
    float scrollX = 0.0f;
};

// Virtual list/grid: item count, stride in px, columns; the pool recycles live children.
struct UiVirtual {
    uint32_t count = 0;
    float itemW = 0.0f;
    float itemH = 0.0f;
    uint32_t columns = 1;
    uint32_t first = 0; // first visible item index (written by pool)
    uint32_t live = 0;  // live entity count (written by pool)
    uint32_t itemAction = 0; // click action id given to every pooled item (0 = items are not clickable)
    uint32_t itemClass = kGuiNoClass; // style class applied to every pooled item by the host
};

// Per-child binding written by the pool: which logical item this recycled entity currently shows.
struct UiVirtualItem {
    uint32_t index = 0;
};

// Overlay (menu, tooltip, dropdown). Anchor is the trigger's box; placement is clamped to the screen.
struct UiOverlay {
    float anchorX = 0.0f;
    float anchorY = 0.0f;
    float anchorW = 0.0f;
    float anchorH = 0.0f;
    uint8_t placement = 0; // 0 below, 1 above, 2 right, 3 left, 4 at pointer
    uint8_t dismissOnOutside = 1;
    uint8_t open = 0;
    uint8_t layer = 0;
};

// Static tooltip of a node (JSON "tooltip"): shown by guiTooltipRun after the hover delay.
struct UiTooltipText {
    char text[kGuiTextCap] = {};
};

// Semantic key of a node (JSON "bind": "actor.pos.x"): C++ addresses the node by it instead of by its DOM path.
struct UiBindKey {
    char key[32] = {};
};

// Click side effect: a release over the clickable flips `target`. A UiOverlay target opens/closes (anchored at this
// entity's box when it opens); any other target is shown/hidden together with its Yoga display.
struct UiToggle {
    uint64_t target = 0;
};

// Splitter grip (JSON "splitter"): dragging it resizes the sibling it borders. A vertical bar (horizontal == 0) sizes the
// width of that sibling, a horizontal bar its height; the size is clamped to minSize..maxSize (maxSize 0 = unbounded).
struct UiSplitter {
    float minSize = 0.0f;
    float maxSize = 0.0f;
    uint8_t horizontal = 0;
    uint8_t resizeNext = 0; // 0: the sibling before the grip is resized, 1: the one after it
    uint8_t pad0 = 0;
    uint8_t pad1 = 0;
};

// Tabbed dock (JSON "dock"): a column holding one UiTabBar (child "tabs", synthesized by the loader) and any number of
// UiDockPanel children, of which exactly one is shown. `active` is that panel's entity id (0 = pick the first open one).
struct UiDock {
    uint64_t active = 0;
    uint8_t shown = 1; // the dock was visible at the last refresh
    uint8_t pad0[7] = {};
};

// A panel inside a UiDock. A closed panel has no visible tab and no content until it is opened again.
struct UiDockPanel {
    uint8_t closed = 0;
};

// Tab header: the entity carries the panel title (UiText) and icon (UiPaint::icon); `panel` is the panel it selects.
struct UiTab {
    uint64_t panel = 0;
};

static_assert(std::is_trivially_copyable_v<UiToggle>);
static_assert(std::is_trivially_copyable_v<UiSplitter> && sizeof(UiSplitter) == 12);
static_assert(std::is_trivially_copyable_v<UiDock> && std::is_trivially_copyable_v<UiDockPanel> && std::is_trivially_copyable_v<UiTab>);
static_assert(std::is_trivially_copyable_v<UiFlex>);
static_assert(std::is_trivially_copyable_v<UiBox> && sizeof(UiBox) == 16);
static_assert(std::is_trivially_copyable_v<UiClip> && sizeof(UiClip) == 16);
static_assert(std::is_trivially_copyable_v<UiPaint>);
static_assert(std::is_trivially_copyable_v<UiText>);
static_assert(std::is_trivially_copyable_v<UiScroll>);
static_assert(std::is_trivially_copyable_v<UiDraggable>);
static_assert(std::is_trivially_copyable_v<UiDragState>);
static_assert(std::is_trivially_copyable_v<UiEditable>);
static_assert(std::is_trivially_copyable_v<UiVirtual>);
static_assert(std::is_trivially_copyable_v<UiOverlay>);

// ---- Tags ----
struct UiHoverable {};
struct UiHovered {};
struct UiPressed {};
struct UiFocusable {};
struct UiFocused {};
struct UiScrollable {};
struct UiPassive {}; // painted but never the hit-test target (floating hints, the marquee, the drop highlight)
struct UiClipChildren {}; // entity clips descendants to its own box (scroll views, popups)
struct UiHidden {};       // entity and subtree skipped by hit test and emitter
struct UiTooltipHost {};  // the overlay entity guiTooltipRun shows tips in
struct UiRoot {};         // layout root (one per screen / overlay)
struct UiTabBar {};       // row that holds the UiTab headers of a UiDock
struct UiTabClose {};     // the close button child of a UiTab

} // namespace burnhope::gui
