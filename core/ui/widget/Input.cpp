#include "Str.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Splitter.hpp"

namespace burnhope {

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
    if (in.escape) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            uiShow(s, uiMenuMetrics(s).opened, false);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            s.layoutDirty = true;
        }
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
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        if (node != nullptr && node->role == static_cast<uint8_t>(UiRole::Field)) {
            const UiField* field = s.ent[hit].try_get<UiField>();
            const float clearW = field != nullptr && field->clear != 0 && field->len > 0 && field->readOnly == 0 ? 22.0f : 0.0f;
            const bool onClear = clearW > 0.0f && in.pointer.x >= s.box[hit].x + s.box[hit].w - field->padR - clearW;
            cursor = onClear ? 6 : 5;
        } else if (s.ent[hit].try_get<UiSplitter>() != nullptr) {
            cursor = splitCursor(s, hit);
        }
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
    uint16_t bandHost = hit < s.count && s.ent[hit].try_get<UiRubber>() != nullptr ? hit : uiAncestorId<UiRubber>(s, hit);
    if (bandHost == kUiNone && s.capture < s.count && s.ent[s.capture].is_alive() && s.ent[s.capture].try_get<UiRubber>() != nullptr) {
        bandHost = s.capture;
    }
    if (bandHost != kUiNone && uiRubberbandTrack(s, bandHost, in)) {
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
    const bool right = (in.pointer.pressed & kPointerRight) != 0;
    if (press && s.grabFn != nullptr && s.grabFn(s.grabUser, hit, in.pointer.x, in.pointer.y)) {
        return UiEvent::None;
    }

    auto inMenu = [&](uint16_t id) {
        for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
            if (p == uiMenuMetrics(s).opened) {
                return true;
            }
        }
        return false;
    };
    uint16_t contextMenu = s.menu;
    for (uint16_t p = hit; p != kUiNone; p = s.parentOf[p]) {
        if (s.contextOf[p] != kUiNone) {
            contextMenu = s.contextOf[p];
            break;
        }
    }
    bool barOpen = false;
    float menuX = in.pointer.x;
    float menuY = in.pointer.y;
    if (press && hit != kUiNone) {
        if (s.ent[hit].try_get<UiMenuBar>() != nullptr && s.contextOf[hit] != kUiNone) {
            barOpen = true;
            contextMenu = s.contextOf[hit];
            menuX = s.box[hit].x;
            menuY = s.box[hit].y + s.box[hit].h;
        }
    }
    bool justOpened = false;
    if (right && hit != kUiNone) {
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        if (node != nullptr && node->role == static_cast<uint8_t>(UiRole::Field) && s.fieldMenu != kUiNone) {
            contextMenu = s.fieldMenu;
            uiFocus(s, hit);
            s.editField = hit;
            s.visualDirty = true;
        }
    }
    bool barOnly = false;
    if (right && hit != kUiNone) {
        for (uint16_t p = hit; p != kUiNone; p = s.parentOf[p]) {
            if (s.ent[p].try_get<UiMenuBar>() != nullptr) {
                barOnly = true;
                break;
            }
        }
    }
    if (((right && !barOnly) || barOpen) && contextMenu != kUiNone) {
        if (barOpen && s.menuOpen && uiMenuMetrics(s).opened == contextMenu) {
            uiShow(s, uiMenuMetrics(s).opened, false);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            justOpened = true;
        } else if (uiMenuMetrics(s).opened != kUiNone && uiMenuMetrics(s).opened != contextMenu) {
            uiShow(s, uiMenuMetrics(s).opened, false);
        }
        if (!justOpened) {
            justOpened = true;
            uiMenuShowAt(s, contextMenu, menuX, menuY);
        }
    }

    if (press && s.menuOpen && !justOpened && !inMenu(hit)) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            uiShow(s, uiMenuMetrics(s).opened, false);
        }
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }

    if (press && hit != s.capture) {
        s.capture = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        s.floatWin = kUiNone;
    }
    const ChromePointer chrome = uiChromePointer(s, in, hit, press && !plugAte, down, release);
    if (chrome.cursor != 0) {
        s.cursor = chrome.cursor;
        if (s.cursorN == 0) {
            uiCursorPush(s, chrome.cursor);
        } else if (s.cursorN == 1) {
            s.cursorStack[0] = chrome.cursor;
        }
    }
    if (press && hit != kUiNone && !plugAte && chrome.took == 0) {
        const UiPaint* p = s.ent[hit].try_get<UiPaint>();
        if (p != nullptr && p->disabled != 0) {
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Close)) {
            return UiEvent::Close;
        } else if (s.ent[hit].try_get<UiSplitter>() != nullptr && splitAlive(s, hit)) {
            s.capture = hit;
        } else if (p != nullptr && s.ent[hit].try_get<UiDrag>() != nullptr) {
            s.capture = hit;
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
            const UiDrag* drag = s.ent[hit].try_get<UiDrag>();
            drag->fn(drag->user, hit, in.pointer.x, in.pointer.y);
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Scroll)) {
            UiBox thumb{};
            if (scrollThumb(s, hit, thumb) && hitBox(thumb, in.pointer.x, in.pointer.y)) {
                s.capture = hit;
                dragScroll(s, hit, in.pointer.y);
            }
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Slider)) {
            s.capture = hit;
            dragSlider(s, hit, in.pointer.x);
        } else if (p != nullptr && focusable(p->role)) {
            uiFocus(s, hit);
            if (p->role == static_cast<uint8_t>(UiRole::Field)) {
                fieldHandlePointer(s, hit, in, true, false);
            } else {
                const UiRange* range = s.ent[hit].try_get<UiRange>();
                if (range != nullptr && range->step > 0.0f) {
                    s.capture = hit;
                    s.grabX = in.pointer.x;
                    s.relative = 1;
                } else {
                    activate(s, hit);
                }
            }
        } else {
            uiFocus(s, kUiNone);
        }
    }

    if (down && s.capture != kUiNone && chrome.took == 0) {
        const UiPaint* cap = s.ent[s.capture].try_get<UiPaint>();
        if (cap != nullptr && s.ent[s.capture].try_get<UiDrag>() != nullptr) {
            const UiDrag* drag = s.ent[s.capture].try_get<UiDrag>();
            drag->fn(drag->user, s.capture, in.pointer.x, in.pointer.y);
        } else if (s.ent[s.capture].try_get<UiSplitter>() != nullptr) {
            splitDrag(s, s.capture, in.pointer.x, in.pointer.y);
        } else if (cap != nullptr && cap->role == static_cast<uint8_t>(UiRole::Button)) {
            const UiRange* range = s.ent[s.capture].try_get<UiRange>();
            if (range != nullptr && range->step > 0.0f) {
                const float x = s.relative != 0 ? s.grabX + in.pointer.dx : in.pointer.x;
                dragSpin(s, s.capture, x);
            }
        } else if (s.ent[s.capture].try_get<UiField>() != nullptr) {
            fieldHandlePointer(s, s.capture, in, false, true);
        } else if (cap != nullptr && cap->role == static_cast<uint8_t>(UiRole::Scroll)) {
            dragScroll(s, s.capture, in.pointer.y);
        } else {
            dragSlider(s, s.capture, in.pointer.x);
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
        uiDock(s).edge = 0;
        if (uiDock(s).hintOn != 0) {
            uiDock(s).hintOn = 0;
            s.visualDirty = true;
        }
    }

    if (in.clicks >= 2 && hit < s.count) {
        const bool onTitle = s.ent[hit].has<UiTitleBar>() || uiAncestorHas<UiTitleBar>(s, hit);
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        const bool chrome = node != nullptr
            && (node->role == static_cast<uint8_t>(UiRole::Close) || node->role == static_cast<uint8_t>(UiRole::Button));
        if (onTitle && !chrome) {
            s.opMaximize = true;
        }
    }

    if (in.wheel != 0.0f && s.hovered != kUiNone) {
        uint16_t hooked = kUiNone;
        for (uint16_t p = s.hovered; p != kUiNone; p = s.parentOf[p]) {
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
        } else {
            const uint16_t owner = scrollOwner(s, s.hovered);
            if (owner != kUiNone) {
                scrollBy(s, owner, in.wheel);
                s.ateWheel = 1;
            }
        }
    }

    if (in.tab != 0 && s.count > 0) {
        uiFocusStep(s, in.shift != 0 ? -1 : 1);
    }

    if (s.focused != kUiNone && in.enter != 0 && !fieldHandleKey(s, s.focused, in)) {
        activate(s, s.focused);
    } else if (s.focused != kUiNone && in.enter == 0) {
        fieldHandleKey(s, s.focused, in);
    }
    if (s.capture != kUiNone) {
        if (s.ent[s.capture].try_get<UiSplitter>() != nullptr && s.laidW > 1.0f && s.laidH > 1.0f) {
            s.clipOn = 1;
            s.clipBox = {0, 0, s.laidW, s.laidH};
        }
    } else {
        s.clipOn = 0;
    }
    splitSync(s);
    uiSyncInput(s);
    return UiEvent::None;
}

} // namespace burnhope
