#include "gui/interaction/InteractTextEdit.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "gui/input/GuiHitTest.hpp"

namespace burnhope::gui {

namespace {

struct Field {
    UiText* text;
    UiEditable* edit;
    const UiBox* box;
    const GuiTextMetrics* metrics;
    uint32_t limit; // maximum byte length
};

std::string_view view(const UiText& t) {
    return std::string_view(t.text, t.len);
}

uint32_t selLo(const UiEditable& e) {
    return std::min(e.caret, e.selAnchor);
}

uint32_t selHi(const UiEditable& e) {
    return std::max(e.caret, e.selAnchor);
}

void capture(const Field& f, GuiEditSnapshot& s) {
    std::memcpy(s.text, f.text->text, kGuiTextCap);
    s.len = f.text->len;
    s.caret = f.edit->caret;
    s.anchor = f.edit->selAnchor;
}

void restore(const Field& f, const GuiEditSnapshot& s) {
    std::memcpy(f.text->text, s.text, kGuiTextCap);
    f.text->len = s.len;
    f.edit->caret = s.caret;
    f.edit->selAnchor = s.anchor;
}

void historyReset(GuiEditHistory& h, uint64_t entity, const Field& f) {
    h.entity = entity;
    h.count = 1;
    h.pos = 0;
    capture(f, h.snap[0]);
}

// Records the field's present state as a new history entry (drops redo entries, then the oldest when full).
void historyPush(GuiEditHistory& h, const Field& f) {
    h.count = static_cast<uint8_t>(h.pos + 1);
    if (h.count == kGuiEditHistoryCap) {
        std::memmove(&h.snap[0], &h.snap[1], sizeof(GuiEditSnapshot) * (kGuiEditHistoryCap - 1));
        --h.count;
    }
    capture(f, h.snap[h.count]);
    ++h.count;
    h.pos = static_cast<uint8_t>(h.count - 1);
}

// Replaces [lo, hi) with `src` (cut at a codepoint boundary so the result stays within `limit`).
bool replaceRange(const Field& f, uint32_t lo, uint32_t hi, const char* src, uint32_t n) {
    UiText& t = *f.text;
    const uint32_t keep = t.len - (hi - lo);
    const uint32_t room = f.limit > keep ? f.limit - keep : 0;
    if (n > room) {
        n = room;
        // Never cut inside a multi-byte sequence.
        while (n > 0 && (static_cast<uint8_t>(src[n]) & 0xC0u) == 0x80u) {
            --n;
        }
    }
    if (n == 0 && lo == hi) {
        return false;
    }
    std::memmove(t.text + lo + n, t.text + hi, t.len - hi);
    std::memcpy(t.text + lo, src, n);
    t.len = static_cast<uint8_t>(keep + n);
    std::memset(t.text + t.len, 0, kGuiTextCap - t.len);
    f.edit->caret = static_cast<uint8_t>(lo + n);
    f.edit->selAnchor = f.edit->caret;
    return true;
}

bool deleteSelection(const Field& f) {
    const uint32_t lo = selLo(*f.edit);
    const uint32_t hi = selHi(*f.edit);
    if (lo == hi) {
        return false;
    }
    return replaceRange(f, lo, hi, "", 0);
}

bool insert(const Field& f, const char* src, uint32_t n, bool allowNewline) {
    char buf[96];
    uint32_t m = 0;
    for (uint32_t i = 0; i < n && m < sizeof(buf); ++i) {
        const char c = src[i];
        if (c == '\n' || c == '\r') {
            if (!allowNewline) {
                continue;
            }
            buf[m++] = '\n';
        } else if (static_cast<uint8_t>(c) >= 0x20 && c != 0x7F) {
            buf[m++] = c;
        }
    }
    if (m == 0) {
        return false;
    }
    const uint32_t lo = selLo(*f.edit);
    const uint32_t hi = selHi(*f.edit);
    return replaceRange(f, lo, hi, buf, m);
}

void moveCaret(const Field& f, uint32_t to, bool extend) {
    f.edit->caret = static_cast<uint8_t>(std::min<uint32_t>(to, f.text->len));
    if (!extend) {
        f.edit->selAnchor = f.edit->caret;
    }
}

float textX(const Field& f, uint32_t caret) {
    return guiTextXFromCaret(*f.metrics, view(*f.text), f.text->size, caret);
}

uint32_t caretAt(const Field& f, float pointerX) {
    const float local = pointerX - f.box->x - kGuiEditPadX + f.edit->scrollX;
    return guiTextCaretFromX(*f.metrics, view(*f.text), f.text->size, local);
}

void keepCaretVisible(const Field& f) {
    const float viewW = std::max(1.0f, f.box->w - 2.0f * kGuiEditPadX);
    const float cx = textX(f, f.edit->caret);
    if (cx - f.edit->scrollX > viewW) {
        f.edit->scrollX = cx - viewW;
    } else if (cx < f.edit->scrollX) {
        f.edit->scrollX = cx;
    }
    const float total = textX(f, f.text->len);
    f.edit->scrollX = std::clamp(f.edit->scrollX, 0.0f, std::max(0.0f, total - viewW));
}

bool mouse(const Field& f, const InputFrame& in, GuiInteractState& state, uint64_t id, flecs::world& world) {
    bool moved = false;
    if ((in.pointer.pressed & kPointerLeft) != 0 && state.capture == 0) {
        const flecs::entity hit = guiAncestorWith<UiEditable>(guiEntityOf(world, state.under));
        if (hit.id() == id) {
            if (in.clicks >= 2) {
                f.edit->selAnchor = 0;
                f.edit->caret = f.text->len;
                state.selecting = 0;
            } else {
                moveCaret(f, caretAt(f, in.pointer.x), in.shift != 0);
                state.selecting = 1;
            }
            moved = true;
        }
    } else if (state.selecting != 0 && (in.pointer.down & kPointerLeft) != 0) {
        const uint8_t caret = static_cast<uint8_t>(caretAt(f, in.pointer.x));
        if (caret != f.edit->caret) {
            f.edit->caret = caret;
            moved = true;
        }
    }
    if ((in.pointer.down & kPointerLeft) == 0) {
        state.selecting = 0;
    }
    return moved;
}

// Keyboard handling; returns true when the text changed. `moved` is set on caret/selection changes.
bool keys(const Field& f, const InputFrame& in, GuiInteractState& state, const GuiClipboard* clip, bool& moved,
          uint64_t id) {
    UiText& t = *f.text;
    UiEditable& ed = *f.edit;
    const bool extend = in.shift != 0;
    const bool multiline = ed.multiline != 0;
    bool changed = false;

    if (in.selectAll != 0) {
        ed.selAnchor = 0;
        ed.caret = t.len;
        moved = true;
    }
    if ((in.copy != 0 || in.cut != 0) && clip != nullptr && clip->write != nullptr && selLo(ed) != selHi(ed)) {
        clip->write(t.text + selLo(ed), selHi(ed) - selLo(ed), clip->user);
        if (in.cut != 0) {
            changed = deleteSelection(f) || changed;
        }
    }
    if (in.pasteOn != 0 && in.pasteLen > 0) {
        changed = insert(f, in.paste, in.pasteLen, multiline) || changed;
    }
    if (in.textLen > 0) {
        changed = insert(f, in.text, in.textLen, false) || changed;
    }
    if (in.backspace != 0) {
        if (selLo(ed) != selHi(ed)) {
            changed = deleteSelection(f) || changed;
        } else if (ed.caret > 0) {
            changed = replaceRange(f, guiTextPrevBoundary(view(t), ed.caret), ed.caret, "", 0) || changed;
        }
    }
    if (in.del != 0) {
        if (selLo(ed) != selHi(ed)) {
            changed = deleteSelection(f) || changed;
        } else if (ed.caret < t.len) {
            changed = replaceRange(f, ed.caret, guiTextNextBoundary(view(t), ed.caret), "", 0) || changed;
        }
    }
    if (in.left != 0) {
        if (!extend && selLo(ed) != selHi(ed)) {
            moveCaret(f, selLo(ed), false);
        } else {
            moveCaret(f, guiTextPrevBoundary(view(t), ed.caret), extend);
        }
        moved = true;
    }
    if (in.right != 0) {
        if (!extend && selLo(ed) != selHi(ed)) {
            moveCaret(f, selHi(ed), false);
        } else {
            moveCaret(f, guiTextNextBoundary(view(t), ed.caret), extend);
        }
        moved = true;
    }
    if (in.home != 0) {
        moveCaret(f, 0, extend);
        moved = true;
    }
    if (in.end != 0) {
        moveCaret(f, t.len, extend);
        moved = true;
    }
    if (in.enter != 0) {
        if (multiline) {
            changed = insert(f, "\n", 1, true) || changed;
        } else {
            guiPushAction(state, id, kGuiActionEditCommit);
        }
    }
    return changed;
}

} // namespace

void interactTextEdit(flecs::world& world, const InputFrame& in, GuiInteractState& state, const GuiStyleSheet& style,
                      const GuiClipboard* clip) {
    const flecs::entity e = guiEntityOf(world, state.focus);
    if (e.id() == 0 || !e.has<UiEditable>()) {
        state.selecting = 0;
        return;
    }
    UiText* text = e.try_get_mut<UiText>();
    UiEditable* edit = e.try_get_mut<UiEditable>();
    const UiBox* box = e.try_get<UiBox>();
    if (text == nullptr || edit == nullptr || box == nullptr) {
        return;
    }
    const Field f{.text = text,
                  .edit = edit,
                  .box = box,
                  .metrics = &style.text,
                  .limit = std::min<uint32_t>(edit->maxLen, kGuiTextCap - 1)};
    text->len = static_cast<uint8_t>(std::min<uint32_t>(text->len, f.limit));
    edit->caret = static_cast<uint8_t>(std::min<uint32_t>(edit->caret, text->len));
    edit->selAnchor = static_cast<uint8_t>(std::min<uint32_t>(edit->selAnchor, text->len));

    GuiEditHistory& hist = world.ensure<GuiEditHistory>();
    if (hist.entity != e.id() || hist.count == 0) {
        historyReset(hist, e.id(), f);
    }

    bool moved = mouse(f, in, state, e.id(), world);
    bool changed = false;
    if (in.undo != 0 && hist.pos > 0) {
        --hist.pos;
        restore(f, hist.snap[hist.pos]);
        changed = true;
        moved = true;
    } else if (in.redo != 0 && hist.pos + 1 < hist.count) {
        ++hist.pos;
        restore(f, hist.snap[hist.pos]);
        changed = true;
        moved = true;
    } else if (keys(f, in, state, clip, moved, e.id())) {
        historyPush(hist, f);
        changed = true;
    }
    if (changed) {
        guiPushAction(state, e.id(), kGuiActionEditChanged);
    }
    if (changed || moved) {
        state.caretTick = 0;
    }
    keepCaretVisible(f);
}

} // namespace burnhope::gui
