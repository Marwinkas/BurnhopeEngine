#include "platform/Check.hpp"
#include "Str.hpp"
#include "ui/State.hpp"

namespace burnhope {

void activate(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint != nullptr && paint->disabled) {
        return;
    }
    uiTrace(s, "activate", id);
    if (const UiAction* act = s.ent[id].try_get<UiAction>(); act != nullptr && act->fn != nullptr) {
        act->fn(act->user, id);
    } else if (s.onClick != nullptr) {
        s.onClick(s.clickUser, id);
    }
}

namespace {

bool probe(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note, bool stop) {
    bool any = false;
    for (UiInputHookFn fn : s.inputHook) {
        if (fn == nullptr) {
            continue;
        }
        UiPhaseNote local = note;
        if (!fn(s, in, id, phase, local)) {
            continue;
        }
        note = local;
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
    [[maybe_unused]] const bool ok = s.inputHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void plantIme(UiState& s, const InputFrame& in) {
    if (s.focused >= s.count) {
        return;
    }
    UiIme ime{};
    ime.len = static_cast<uint8_t>(strCopyCap(ime.bytes, sizeof(ime.bytes), in.editing));
    s.ent[s.focused].set<UiIme>(ime);
}

UiEvent uiApplyInput(UiState& s, const InputFrame& in) {
    s.ateWheel = 0;
    const uint16_t hit = hitTest(s, in.pointer.x, in.pointer.y);
    if (hit != s.hovered) {
        s.hovered = hit;
        s.visualDirty = true;
    }
    UiPhaseNote unused{};
    if (in.escape) {
        probe(s, in, hit, kUiInEscape, unused, false);
        uiReleaseCapture(s);
        s.relative = 0;
        uiFocus(s, kUiNone);
    }

    UiPhaseNote pointed{};
    if (hit != kUiNone) {
        probe(s, in, hit, kUiInCursor, pointed, true);
    }
    s.cursor = pointed.cursor;
    if (s.cursorN == 0 && pointed.cursor != 0) {
        uiCursorPush(s, pointed.cursor);
    } else if (s.cursorN == 1) {
        s.cursorStack[0] = pointed.cursor;
        if (pointed.cursor == 0) {
            s.cursorN = 0;
        }
    }
    if (probe(s, in, hit, kUiInEarly, unused, true)) {
        plantIme(s, in);
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
        plantIme(s, in);
        return UiEvent::None;
    }

    probe(s, in, hit, kUiPhase4, unused, false);

    if (press && hit != s.capture) {
        uiReleaseCapture(s);
    }
    UiPhaseNote chrome{};
    chrome.plugAte = plugAte ? 1 : 0;
    probe(s, in, hit, kUiPhase5, chrome, true);
    const bool chromeTook = chrome.took != 0;
    if (chrome.cursor != 0) {
        pushCursor(s, chrome.cursor);
    }
    if (press && hit != kUiNone && !plugAte && !chromeTook) {
        const UiPaint* p = s.ent[hit].try_get<UiPaint>();
        if (p != nullptr && p->disabled) {
        } else if (probe(s, in, hit, kUiInPress, unused, true)) {
        } else if (p != nullptr && s.ent[hit].try_get<UiDrag>() != nullptr) {
            uiCapture(s, hit);
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

    if ((in.pointer.pressed & kPointerRight) != 0 && hit != kUiNone && s.ent[hit].try_get<UiDragRight>() != nullptr) {
        s.rightGrab = hit;
    }
    if (s.rightGrab != kUiNone) {
        const UiDragRight* orbit = s.rightGrab < s.count ? s.ent[s.rightGrab].try_get<UiDragRight>() : nullptr;
        if ((in.pointer.down & kPointerRight) != 0 && orbit != nullptr) {
            orbit->fn(orbit->user, s.rightGrab, in.pointer.dx, in.pointer.dy);
        } else {
            s.rightGrab = kUiNone;
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
        uiReleaseCapture(s);
        s.relative = 0;
        probe(s, in, hit, kUiInRelease, unused, false);
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
    plantIme(s, in);
    return UiEvent::None;
}

} // namespace burnhope
