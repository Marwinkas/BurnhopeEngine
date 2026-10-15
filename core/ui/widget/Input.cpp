#include "Str.hpp"
#include "ui/State.hpp"

namespace burnhope {
namespace {

bool probe(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor, bool stop) {
    const UiInputHooks* hooks = s.world.try_get<UiInputHooks>();
    if (hooks == nullptr) {
        return false;
    }
    bool any = false;
    for (uint8_t i = 0; i < hooks->n; ++i) {
        if (hooks->fn[i] == nullptr) {
            continue;
        }
        uint8_t local = cursor;
        if (!hooks->fn[i](s, in, id, phase, local)) {
            continue;
        }
        cursor = local;
        any = true;
        if (stop) {
            return true;
        }
    }
    return any;
}

void pushCursor(UiState& s, uint8_t cursor) {
    s.cursor = cursor;
    if (s.cursorN == 0) {
        uiCursorPush(s, cursor);
    } else if (s.cursorN == 1) {
        s.cursorStack[0] = cursor;
    }
}

} // namespace

void uiInputAdd(UiState& s, UiInputHookFn fn) {
    if (fn == nullptr) {
        return;
    }
    UiInputHooks& hooks = s.world.ensure<UiInputHooks>();
    for (uint8_t i = 0; i < hooks.n; ++i) {
        if (hooks.fn[i] == fn) {
            return;
        }
    }
    if (hooks.n < 8) {
        hooks.fn[hooks.n++] = fn;
    }
}

bool uiListen(UiState& s, UiValueFn fn, void* user) {
    if (fn == nullptr || s.valueN >= 4) {
        return false;
    }
    s.valueFn[s.valueN] = fn;
    s.valueUser[s.valueN] = user;
    ++s.valueN;
    return true;
}

UiEvent uiApplyInput(UiState& s, const InputFrame& in) {
    s.ateWheel = 0;
    s.pasteBufLen = static_cast<uint8_t>(strCopyCap(s.pasteBuf, sizeof(s.pasteBuf), in.paste));
    s.imeLen = static_cast<uint8_t>(strCopyCap(s.ime, sizeof(s.ime), in.editing));
    const uint16_t hit = hitTest(s, in.pointer.x, in.pointer.y);
    if (hit != s.hovered) {
        s.hovered = hit;
        s.visualDirty = true;
    }
    uint8_t unused = 0;
    if (in.escape) {
        probe(s, in, hit, kUiInEscape, unused, false);
        if (s.dropBus.active != 0 || s.dragId != kUiNone) {
            uiDragEnd(s, kUiNone);
            s.dragId = kUiNone;
        }
        s.capture = kUiNone;
        s.relative = 0;
        uiFocus(s, kUiNone);
    }

    uint8_t cursor = 0;
    if (hit != kUiNone) {
        probe(s, in, hit, kUiInCursor, cursor, true);
    }
    s.cursor = cursor;
    if (s.cursorN == 0 && cursor != 0) {
        uiCursorPush(s, cursor);
    } else if (s.cursorN == 1) {
        s.cursorStack[0] = cursor;
        if (cursor == 0) {
            s.cursorN = 0;
        }
    }
    if (probe(s, in, hit, kUiInEarly, unused, true)) {
        return UiEvent::None;
    }
    const bool plugAte = s.plug.handle != nullptr && s.plug.handle(s, in);
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    if (inputKeyEdge(in, kScanF3)) {
        s.dumpLayout = 1;
    }
    if (press) {
        uiTrace(s, plugAte ? "press-eaten" : "press", hit);
    }
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    if (press && s.grabFn != nullptr && s.grabFn(s.grabUser, hit, in.pointer.x, in.pointer.y)) {
        return UiEvent::None;
    }

    probe(s, in, hit, kUiInMenu, unused, false);

    if (press && hit != s.capture) {
        s.capture = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        s.floatWin = kUiNone;
    }
    uint8_t chromeCursor = plugAte ? 0x80u : 0u;
    probe(s, in, hit, kUiInChrome, chromeCursor, true);
    const bool chromeTook = (chromeCursor & 0x40u) != 0;
    const uint8_t chromeKind = static_cast<uint8_t>(chromeCursor & 0x0fu);
    if (chromeKind != 0) {
        pushCursor(s, chromeKind);
    }
    if (press && hit != kUiNone && !plugAte && !chromeTook) {
        const UiPaint* p = s.ent[hit].try_get<UiPaint>();
        if (p != nullptr && p->disabled) {
        } else if (probe(s, in, hit, kUiInPress, unused, true)) {
        } else if (p != nullptr && s.ent[hit].try_get<UiDrag>() != nullptr) {
            s.capture = hit;
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
            const UiDrag* drag = s.ent[hit].try_get<UiDrag>();
            drag->fn(drag->user, hit, in.pointer.x, in.pointer.y);
        } else if (probe(s, in, hit, kUiInPressLate, unused, true)) {
        } else if (s.ent[hit].has<UiFocusable>()) {
            uiFocus(s, hit);
            if (!probe(s, in, hit, kUiInFocus, unused, true)) {
                activate(s, hit);
            }
        } else {
            uiFocus(s, kUiNone);
        }
    }

    if (down && s.capture != kUiNone && !chromeTook) {
        const UiPaint* cap = s.ent[s.capture].try_get<UiPaint>();
        if (cap != nullptr && s.ent[s.capture].try_get<UiDrag>() != nullptr) {
            const UiDrag* drag = s.ent[s.capture].try_get<UiDrag>();
            drag->fn(drag->user, s.capture, in.pointer.x, in.pointer.y);
        } else {
            probe(s, in, s.capture, kUiInDrag, unused, true);
        }
    }
    if (release || !down) {
        s.capture = kUiNone;
        s.relative = 0;
        if (s.dragId != kUiNone) {
            uiDragEnd(s, hit);
        }
        s.dragId = kUiNone;
        s.floatWin = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        probe(s, in, hit, kUiInRelease, unused, false);
    }

    if (in.clicks >= 2 && hit < s.count) {
        const bool onTitle = s.ent[hit].has<UiTitleBar>() || uiAncestorHas<UiTitleBar>(s, hit);
        if (onTitle && !s.ent[hit].has<UiHoverable>()) {
            s.opMaximize = true;
        }
    }

    if (in.wheel != 0.0f && s.hovered != kUiNone) {
        uint16_t hooked = kUiNone;
        for (uint16_t p = s.hovered; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].try_get<UiWheel>() != nullptr) {
                hooked = p;
                break;
            }
        }
        if (hooked != kUiNone) {
            const UiWheel* wheel = s.ent[hooked].try_get<UiWheel>();
            if (wheel != nullptr && wheel->fn != nullptr) {
                wheel->fn(wheel->user, hooked, in.wheel, in.pointer.x, in.pointer.y);
            }
            s.ateWheel = 1;
        } else if (probe(s, in, s.hovered, kUiInWheel, unused, true)) {
            s.ateWheel = 1;
        }
    }

    if (in.tab != 0 && s.count > 0) {
        uiFocusStep(s, in.shift != 0 ? -1 : 1);
    }

    if (s.focused != kUiNone) {
        const bool ateKey = probe(s, in, s.focused, kUiInKey, unused, true);
        if (in.enter != 0 && !ateKey) {
            activate(s, s.focused);
        }
    }
    if (s.capture != kUiNone) {
        if (probe(s, in, s.capture, kUiInClip, unused, true)) {
            s.clipOn = 1;
            s.clipBox = {0, 0, s.laidW, s.laidH};
        }
    } else {
        s.clipOn = 0;
    }
    probe(s, in, hit, kUiInSync, unused, false);
    uiSyncInput(s);
    return UiEvent::None;
}

} // namespace burnhope
