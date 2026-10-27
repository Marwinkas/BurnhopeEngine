#pragma once

#include <cstdint>
#include <type_traits>

#include <flecs.h>
#include <yoga/Yoga.h>

namespace burnhope::gui {

constexpr uint32_t kGuiTextCap = 128;
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
    uint8_t pad0 = 0;
    float radius = 0.0f;
    float borderW = 0.0f;
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; // multiplied into the token color; hover tints write here
};

struct UiText {
    char text[kGuiTextCap] = {};
    uint8_t len = 0;
    uint8_t align = 0; // 0 left, 1 center, 2 right
    uint8_t size = 14; // px
    uint8_t pad0 = 0;
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

// Click side effect: a release over the clickable flips `target`. A UiOverlay target opens/closes (anchored at this
// entity's box when it opens); any other target is shown/hidden together with its Yoga display.
struct UiToggle {
    uint64_t target = 0;
};

static_assert(std::is_trivially_copyable_v<UiToggle>);
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
struct UiClipChildren {}; // entity clips descendants to its own box (scroll views, popups)
struct UiHidden {};       // entity and subtree skipped by hit test and emitter
struct UiRoot {};         // layout root (one per screen / overlay)

} // namespace burnhope::gui
