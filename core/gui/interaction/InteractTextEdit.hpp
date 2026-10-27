#pragma once

#include <cstdint>
#include <type_traits>

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "gui/style/GuiStyleSheet.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Host clipboard sink. Paste text is delivered through InputFrame.paste (pasteOn/pasteLen), never pulled from here.
struct GuiClipboard {
    void (*write)(const char* text, uint32_t len, void* user) = nullptr;
    void* user = nullptr;
};

// Horizontal inset between a field's box and its text; the emitter must draw text at box.x + kGuiEditPadX - scrollX.
constexpr float kGuiEditPadX = 6.0f;
constexpr uint32_t kGuiEditHistoryCap = 16;

struct GuiEditSnapshot {
    char text[kGuiTextCap] = {};
    uint8_t len = 0;
    uint8_t caret = 0;
    uint8_t anchor = 0;
    uint8_t pad0 = 0;
};

// Undo/redo ring of the focused field, stored as a world singleton. `pos` indexes the current state inside
// snap[0..count); the oldest entry drops when the 16 slots are full. Reset whenever focus moves to another entity.
struct GuiEditHistory {
    uint64_t entity = 0;
    GuiEditSnapshot snap[kGuiEditHistoryCap] = {};
    uint8_t count = 0;
    uint8_t pos = 0;
    uint8_t pad0 = 0;
    uint8_t pad1 = 0;
};

static_assert(std::is_trivially_copyable_v<GuiEditSnapshot>);
static_assert(std::is_trivially_copyable_v<GuiEditHistory>);

// Edits the focused UiEditable+UiText entity: typing (InputFrame.text), paste, backspace/delete, arrows, home/end,
// shift-selection, select all, copy/cut (through `clip`, may be null), undo/redo, mouse caret and drag selection
// (double click selects all). Pushes kGuiActionEditChanged after a change and kGuiActionEditCommit on Enter in a
// single-line field. Resets state.caretTick on any caret movement.
void interactTextEdit(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style,
                      const GuiClipboard* clip);

} // namespace burnhope::gui
