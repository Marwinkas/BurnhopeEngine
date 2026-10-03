#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

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
    tickPhoto(s);
    syncColor(s);
    s.pasteBufLen = in.pasteLen < 95 ? in.pasteLen : 95;
    for (uint8_t i = 0; i < s.pasteBufLen; ++i) {
        s.pasteBuf[i] = in.paste[i];
    }
    s.pasteBuf[s.pasteBufLen] = '\0';
    s.imeLen = in.editingLen < 63 ? in.editingLen : 63;
    for (uint8_t i = 0; i < s.imeLen; ++i) {
        s.ime[i] = in.editing[i];
    }
    s.ime[s.imeLen] = '\0';
    const uint16_t hit = hitTest(s, in.pointer.x, in.pointer.y);
    if (hit != s.hovered) {
        s.hovered = hit;
        s.visualDirty = true;
    }
    if (in.escape) {
        if (s.openedMenu != kUiNone) {
            uiShow(s, s.openedMenu, false);
            s.openedMenu = kUiNone;
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

    auto edgeAt = [&](const UiBox& b, float x, float y) -> int {
        const float m = 6.0f;
        if (!hitBox(b, x, y)) {
            return 0;
        }
        const bool left = x < b.x + m;
        const bool right = x > b.x + b.w - m;
        const bool top = y < b.y + m;
        const bool bottom = y > b.y + b.h - m;
        if (!left && !right && !top && !bottom) {
            return 0;
        }
        return (left ? 1 : 0) | (right ? 2 : 0) | (top ? 4 : 0) | (bottom ? 8 : 0);
    };
    int hoverEdge = 0;
    uint16_t frame = kUiNone;
    if (s.resizeId == kUiNone) {
        for (uint16_t n = s.count; n > 0; --n) {
            const uint16_t id = static_cast<uint16_t>(n - 1);
            if (!uiIsOverlay(s, id) || s.yoga[id] == nullptr) {
                continue;
            }
            if (YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone) {
                continue;
            }
            const UiPaint* node = s.ent[id].try_get<UiPaint>();
            const bool frameNode = node != nullptr && node->tag == -15;
            const bool floatingDock = id == s.dockPanel && s.parentOf[id] == 0;
            if (!frameNode && !floatingDock) {
                continue;
            }
            const int edge = edgeAt(s.box[id], in.pointer.x, in.pointer.y);
            if (edge == 0) {
                continue;
            }
            frame = id;
            hoverEdge = edge;
            break;
        }
    } else {
        frame = s.resizeId;
        hoverEdge = s.resizeEdge;
    }
    uint8_t cursor = 0;
    if (hoverEdge != 0) {
        const bool across = (hoverEdge & 3) != 0;
        const bool down = (hoverEdge & 12) != 0;
        if (across && down) {
            const bool nw = ((hoverEdge & 1) != 0 && (hoverEdge & 4) != 0) || ((hoverEdge & 2) != 0 && (hoverEdge & 8) != 0);
            cursor = nw ? 4 : 3;
        } else if (across) {
            cursor = 1;
        } else {
            cursor = 2;
        }
        } else if (hit != kUiNone) {
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        if (node != nullptr && node->role == static_cast<uint8_t>(UiRole::Field)) {
            const UiField* field = s.ent[hit].try_get<UiField>();
            const float clearW = field != nullptr && field->clear != 0 && field->len > 0 && field->readOnly == 0 ? 22.0f : 0.0f;
            const bool onClear = clearW > 0.0f && in.pointer.x >= s.box[hit].x + s.box[hit].w - field->padR - clearW;
            cursor = onClear ? 6 : 5;
        } else if (node != nullptr && node->tag == -5) {
            cursor = 1;
        }
    }
    if (s.colorDrop != 0 || (s.book != nullptr && s.book->drop != 0)) {
        cursor = 7;
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
    const uint8_t wasEdge = s.edgeOn;
    s.edgeOn = 0;
    if (hoverEdge != 0 && frame != kUiNone && frame < s.count) {
        const UiBox& b = s.box[frame];
        s.edgeOn = 1;
        if ((hoverEdge & 1) != 0) {
            s.edgeLine = {b.x, b.y, 2.0f, b.h};
        } else if ((hoverEdge & 2) != 0) {
            s.edgeLine = {b.x + b.w - 2.0f, b.y, 2.0f, b.h};
        } else if ((hoverEdge & 4) != 0) {
            s.edgeLine = {b.x, b.y, b.w, 2.0f};
        } else {
            s.edgeLine = {b.x, b.y + b.h - 2.0f, b.w, 2.0f};
        }
    }
    if (s.edgeOn != wasEdge) {
        s.visualDirty = true;
    }

    const bool filesAte = s.plug.handle != nullptr && s.plug.handle(s, in);
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    if (inputKeyEdge(in, kScanF3)) {
        s.dumpLayout = 1;
    }
    if (press) {
        uiTrace(s, filesAte ? "press-eaten" : "press", hit);
    }
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    const bool right = (in.pointer.pressed & kPointerRight) != 0;
    if (press && s.grabFn != nullptr && s.grabFn(s.grabUser, hit, in.pointer.x, in.pointer.y)) {
        return UiEvent::None;
    }

    auto inMenu = [&](uint16_t id) {
        for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
            if (p == s.openedMenu) {
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
        const UiPaint* bar = s.ent[hit].try_get<UiPaint>();
        if (bar != nullptr && bar->tag == -7 && s.contextOf[hit] != kUiNone) {
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
    if ((right || barOpen) && contextMenu != kUiNone) {
        if (barOpen && s.menuOpen && s.openedMenu == contextMenu) {
            uiShow(s, s.openedMenu, false);
            s.openedMenu = kUiNone;
            s.menuOpen = false;
            justOpened = true;
        } else if (s.openedMenu != kUiNone && s.openedMenu != contextMenu) {
            uiShow(s, s.openedMenu, false);
        }
        if (!justOpened) {
        s.openedMenu = contextMenu;
        justOpened = true;
        auto* flex = s.ent[contextMenu].try_get_mut<UiFlex>();
        if (flex != nullptr) {
            float x = menuX;
            float y = menuY;
            uint32_t items = 0;
            for (uint16_t id = 0; id < s.count; ++id) {
                if (s.parentOf[id] == contextMenu) {
                    ++items;
                }
            }
            const float menuH = s.menuPad * 2.0f + static_cast<float>(items) * s.menuItemH
                + static_cast<float>(items > 0 ? items - 1 : 0) * s.menuGap;
            if (x + s.menuW > s.laidW) {
                x = s.laidW - s.menuW;
            }
            if (y + menuH > s.laidH) {
                y = s.laidH - menuH;
            }
            if (x < 4.0f) {
                x = 4.0f;
            }
            if (y < 4.0f) {
                y = 4.0f;
            }
            flex->position = 1;
            flex->posX = x;
            flex->posY = y;
            YGNodeStyleSetDisplay(s.yoga[contextMenu], YGDisplayFlex);
            s.menuOpen = true;
            s.layoutDirty = true;
            s.visualDirty = true;
            const UiPaint* menuPaint = s.ent[contextMenu].try_get<UiPaint>();
            if (menuPaint != nullptr) {
                uiMotionTo(s, contextMenu, menuPaint->r, menuPaint->g, menuPaint->b, 1.0f);
                s.anim[contextMenu].fa = 0.0f;
            }
        }
        }
    }

    if (press && s.menuOpen && !justOpened && !inMenu(hit)) {
        if (s.openedMenu != kUiNone) {
            uiShow(s, s.openedMenu, false);
        }
        s.openedMenu = kUiNone;
        s.menuOpen = false;
    }

    if (press && hit != kUiNone && !filesAte) {
        const UiPaint* p = s.ent[hit].try_get<UiPaint>();
        if (p != nullptr && p->disabled != 0) {
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Close)) {
            return UiEvent::Close;
        } else if (press && hoverEdge != 0 && frame != kUiNone) {
            s.capture = frame;
            s.resizeId = frame;
            s.resizeEdge = static_cast<uint8_t>(hoverEdge);
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
        } else if (p != nullptr && p->tag == -6 && s.parentOf[hit] != kUiNone) {
            const uint16_t panel = s.parentOf[hit];
            s.capture = hit;
            s.floatWin = panel;
            s.grabX = in.pointer.x - s.box[panel].x;
            s.grabY = in.pointer.y - s.box[panel].y;
        } else if (p != nullptr && p->tag == -5 && splitTarget(s, hit) != kUiNone) {
            s.capture = hit;
        } else if (p != nullptr && s.ent[hit].try_get<UiDrag>() != nullptr) {
            s.capture = hit;
            const UiDrag* drag = s.ent[hit].try_get<UiDrag>();
            drag->fn(drag->user, hit, in.pointer.x, in.pointer.y);
        } else if (p != nullptr && p->tag == -90) {
            auto* image = s.ent[hit].try_get_mut<UiImage>();
            float u = 0.0f;
            float v = 0.0f;
            if (image != nullptr && photoMinimap(s.box[hit], in.pointer.x, in.pointer.y, u, v)) {
                image->viewX = u - image->viewW * 0.5f;
                image->viewY = v - image->viewH * 0.5f;
                photoClamp(*image);
                s.visualDirty = true;
            } else {
                s.capture = hit;
                s.grabX = in.pointer.x;
                s.grabY = in.pointer.y;
            }
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Scroll)) {
            s.capture = hit;
            dragScroll(s, hit, in.pointer.y);
        } else if (p != nullptr && p->role == static_cast<uint8_t>(UiRole::Slider)) {
            s.capture = hit;
            dragSlider(s, hit, in.pointer.x);
        } else if (p != nullptr && focusable(p->role)) {
            uiFocus(s, hit);
            if (p->role == static_cast<uint8_t>(UiRole::Field)) {
                auto* field = s.ent[hit].try_get_mut<UiField>();
                s.editField = hit;
                const float clearW = field->clear != 0 && field->len > 0 && field->readOnly == 0 ? 22.0f : 0.0f;
                if (clearW > 0.0f && in.pointer.x >= s.box[hit].x + s.box[hit].w - field->padR - clearW) {
                    field->len = 0;
                    field->caret = 0;
                    field->anchor = 0;
                    field->scroll = 0.0f;
                    field->bytes[0] = '\0';
                    s.visualDirty = true;
                } else if (in.clicks >= 3) {
                    field->anchor = 0;
                    field->caret = field->len;
                    s.visualDirty = true;
                } else if (in.clicks >= 2) {
                    const float local = in.pointer.x - (s.box[hit].x + field->padL) + (field->multi == 0 ? field->scroll : 0.0f);
                    const float localY = in.pointer.y - s.box[hit].y + (field->multi != 0 ? field->scroll : 0.0f);
                    const float viewW = s.box[hit].w - field->padL - field->padR;
                    fieldSelectWord(*field, field->multi != 0 ? fieldCaretAt2(s, *field, local, localY, viewW) : fieldCaretAt(s, *field, local));
                    s.visualDirty = true;
                } else {
                    const float local = in.pointer.x - (s.box[hit].x + field->padL) + (field->multi == 0 ? field->scroll : 0.0f);
                    const float localY = in.pointer.y - s.box[hit].y + (field->multi != 0 ? field->scroll : 0.0f);
                    const float viewW = s.box[hit].w - field->padL - field->padR;
                    field->caret = field->multi != 0 ? fieldCaretAt2(s, *field, local, localY, viewW) : fieldCaretAt(s, *field, local);
                    field->anchor = field->caret;
                    s.capture = hit;
                    const float view = s.box[hit].w - field->padL - field->padR - clearW;
                    fieldReveal(s, *field, view);
                    s.visualDirty = true;
                }
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

    if (down && s.capture != kUiNone) {
        const UiPaint* cap = s.ent[s.capture].try_get<UiPaint>();
        if (s.resizeId != kUiNone && s.resizeId < s.count) {
            auto* flex = s.ent[s.resizeId].try_get_mut<UiFlex>();
            if (flex != nullptr) {
                float x = flex->posX;
                float y = flex->posY;
                float w = s.box[s.resizeId].w;
                float h = s.box[s.resizeId].h;
                const float dx = in.pointer.x - s.grabX;
                const float dy = in.pointer.y - s.grabY;
                s.grabX = in.pointer.x;
                s.grabY = in.pointer.y;
                if ((s.resizeEdge & 1) != 0) {
                    x += dx;
                    w -= dx;
                }
                if ((s.resizeEdge & 2) != 0) {
                    w += dx;
                }
                if ((s.resizeEdge & 4) != 0) {
                    y += dy;
                    h -= dy;
                }
                if ((s.resizeEdge & 8) != 0) {
                    h += dy;
                }
                if (w < 220.0f) {
                    if ((s.resizeEdge & 1) != 0) {
                        x -= 220.0f - w;
                    }
                    w = 220.0f;
                }
                if (h < 120.0f) {
                    if ((s.resizeEdge & 4) != 0) {
                        y -= 120.0f - h;
                    }
                    h = 120.0f;
                }
                uiPlace(s, s.resizeId, true, x, y, w, h);
            }
        } else if (cap != nullptr && cap->tag == -6 && s.floatWin != kUiNone && s.floatWin < s.count) {
            const float x = in.pointer.x - s.grabX;
            const float y = in.pointer.y - s.grabY;
            const float dx = x - s.box[s.floatWin].x;
            const float dy = y - s.box[s.floatWin].y;
            if (dx * dx + dy * dy > 16.0f || s.parentOf[s.floatWin] == 0) {
                const float w = s.box[s.floatWin].w > 40.0f ? s.box[s.floatWin].w : 280.0f;
                const float h = s.box[s.floatWin].h > 40.0f ? s.box[s.floatWin].h : 180.0f;
                if (s.parentOf[s.floatWin] != 0) {
                    uiReparent(s, s.floatWin, 0);
                }
                uiPlace(s, s.floatWin, true, x, y, w, h);
            }
            const uint8_t prevEdge = s.dockEdge;
            s.dockEdge = 0;
            s.hintOn = 0;
            if (s.parentOf[s.floatWin] == 0 && s.floatWin == s.dockPanel && s.dockHome != kUiNone && s.dockHome < s.count && hitBox(s.box[s.dockHome], in.pointer.x, in.pointer.y)) {
                const UiBox& home = s.box[s.dockHome];
                const float lx = home.w > 1.0f ? (in.pointer.x - home.x) / home.w : 0.5f;
                const float ly = home.h > 1.0f ? (in.pointer.y - home.y) / home.h : 0.5f;
                uint8_t edge = 0;
                if (lx < 0.28f && (s.dockEdges & 1) != 0) {
                    edge = 1;
                } else if (lx > 0.72f && (s.dockEdges & 2) != 0) {
                    edge = 2;
                } else if (ly < 0.28f && (s.dockEdges & 4) != 0) {
                    edge = 3;
                } else if (ly > 0.72f && (s.dockEdges & 8) != 0) {
                    edge = 4;
                }
                s.dockEdge = edge;
                if (edge != 0) {
                    s.hintOn = 1;
                    if (edge == 1) {
                        s.hint = {home.x, home.y, home.w * 0.34f, home.h};
                    } else if (edge == 2) {
                        s.hint = {home.x + home.w * 0.66f, home.y, home.w * 0.34f, home.h};
                    } else if (edge == 3) {
                        s.hint = {home.x, home.y, home.w, home.h * 0.34f};
                    } else {
                        s.hint = {home.x, home.y + home.h * 0.66f, home.w, home.h * 0.34f};
                    }
                }
            }
            if (s.dockEdge != prevEdge) {
                s.visualDirty = true;
            }
        } else if (cap != nullptr && s.ent[s.capture].try_get<UiDrag>() != nullptr) {
            const UiDrag* drag = s.ent[s.capture].try_get<UiDrag>();
            drag->fn(drag->user, s.capture, in.pointer.x, in.pointer.y);
        } else if (cap != nullptr && cap->tag == -90) {
            photoPan(s, s.capture, in.pointer.x, in.pointer.y);
        } else if (s.capture == s.fileView) {
        } else if (cap != nullptr && cap->tag == -5) {
            const uint16_t pane = splitTarget(s, s.capture);
            auto* flex = pane != kUiNone ? s.ent[pane].try_get_mut<UiFlex>() : nullptr;
            if (flex != nullptr) {
                float w = in.pointer.x - s.box[pane].x;
                if (w < 120.0f) {
                    w = 120.0f;
                }
                if (w > 480.0f) {
                    w = 480.0f;
                }
                flex->widthMode = static_cast<uint8_t>(UiSize::Px);
                flex->width = w;
                s.layoutDirty = true;
            }
        } else if (cap != nullptr && cap->role == static_cast<uint8_t>(UiRole::Button)) {
            const UiRange* range = s.ent[s.capture].try_get<UiRange>();
            if (range != nullptr && range->step > 0.0f) {
                const float x = s.relative != 0 ? s.grabX + in.pointer.dx : in.pointer.x;
                dragSpin(s, s.capture, x);
            }
        } else if (s.ent[s.capture].try_get<UiField>() != nullptr) {
            auto* field = s.ent[s.capture].try_get_mut<UiField>();
            const float local = in.pointer.x - (s.box[s.capture].x + field->padL) + (field->multi == 0 ? field->scroll : 0.0f);
            const float localY = in.pointer.y - s.box[s.capture].y + (field->multi != 0 ? field->scroll : 0.0f);
            const float viewW = s.box[s.capture].w - field->padL - field->padR;
            field->caret = field->multi != 0 ? fieldCaretAt2(s, *field, local, localY, viewW) : fieldCaretAt(s, *field, local);
            const float clearW = field->clear != 0 && field->len > 0 ? 22.0f : 0.0f;
            fieldReveal(s, *field, s.box[s.capture].w - field->padL - field->padR - clearW);
            s.visualDirty = true;
        } else if (cap != nullptr && cap->role == static_cast<uint8_t>(UiRole::Scroll)) {
            dragScroll(s, s.capture, in.pointer.y);
        } else {
            dragSlider(s, s.capture, in.pointer.x);
        }
    }
    if (press) {
        const uint16_t item = listItemOf(s, hit);
        s.dragId = item;
        if (item != kUiNone) {
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
        }
        if (item != kUiNone && s.statusLabel != kUiNone) {
            const char* name = "ITEM";
            uint8_t nameLen = 4;
            for (uint16_t id = 0; id < s.count; ++id) {
                if (s.parentOf[id] != item) {
                    continue;
                }
                const UiText* text = s.ent[id].try_get<UiText>();
                if (text != nullptr && text->len > 0) {
                    name = text->bytes;
                    nameLen = text->len;
                    break;
                }
            }
            auto* status = s.ent[s.statusLabel].try_get_mut<UiText>();
            if (status != nullptr) {
                char buf[32] = {'I', 'D', ' '};
                uint8_t n = 3;
                uint16_t value = item;
                char digits[8]{};
                int d = 0;
                do {
                    digits[d++] = static_cast<char>('0' + (value % 10));
                    value = static_cast<uint16_t>(value / 10);
                } while (value > 0 && d < 8);
                while (d > 0 && n < 30) {
                    buf[n++] = digits[--d];
                }
                if (n < 30) {
                    buf[n++] = ' ';
                }
                for (uint8_t i = 0; i < nameLen && n < 31; ++i) {
                    buf[n++] = name[i];
                }
                buf[n] = '\0';
                copyText(status->bytes, status->len, buf);
                s.pickedId = item;
                copyText(s.pickedName, s.pickedLen, name);
                auto* flex = s.ent[s.statusLabel].try_get_mut<UiFlex>();
                if (flex != nullptr) {
                    flex->width = static_cast<float>(status->len) * s.look.advance;
                }
                s.layoutDirty = true;
                s.visualDirty = true;
            }
        }
    }
    if (down && s.dragId != kUiNone && s.capture == kUiNone) {
        const float dx = in.pointer.x - s.grabX;
        const float dy = in.pointer.y - s.grabY;
        if (dx * dx + dy * dy >= 16.0f) {
            if (s.dropBus.active == 0) {
                uiDragBegin(s, s.dragId, "item", nullptr);
            }
            const uint16_t over = listItemOf(s, hit);
            if (over != kUiNone && over != s.dragId) {
                const UiBox& b = s.box[over];
                const bool after = in.pointer.y > b.y + b.h * 0.5f;
                moveListItem(s, s.dragId, over, after);
            }
        }
    }
    if (release) {
        if (s.dockEdge != 0 && s.floatWin == s.dockPanel) {
            uiDockTo(s, s.dockEdge);
        }
        s.capture = kUiNone;
        s.relative = 0;
        if (s.dragId != kUiNone) {
            uiDragEnd(s, hit);
        }
        s.dragId = kUiNone;
        s.floatWin = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        s.dockEdge = 0;
        if (s.hintOn != 0) {
            s.hintOn = 0;
            s.visualDirty = true;
        }
    }

    if (in.clicks >= 2 && in.pointer.y < s.look.titleBar && hit != kUiNone) {
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        const bool chrome = node != nullptr
            && (node->role == static_cast<uint8_t>(UiRole::Close) || node->tag == -2 || node->tag == -3);
        if (!chrome) {
            s.opMaximize = true;
        }
    }

    if (in.wheel != 0.0f && s.hovered != kUiNone && s.hovered != s.fileView) {
        uint16_t photo = kUiNone;
        for (uint16_t p = s.hovered; p != kUiNone; p = s.parentOf[p]) {
            const UiPaint* paint = s.ent[p].try_get<UiPaint>();
            if (paint != nullptr && paint->tag == -90) {
                photo = p;
                break;
            }
        }
        if (photo != kUiNone) {
            photoZoom(s, photo, in.wheel, in.pointer.x, in.pointer.y);
        } else {
            const uint16_t owner = scrollOwner(s, s.hovered);
            if (owner != kUiNone) {
                scrollBy(s, owner, in.wheel);
            }
        }
    }

    if (in.tab != 0 && s.count > 0) {
        uiFocusStep(s, in.shift != 0 ? -1 : 1);
    }

    if (s.focused != kUiNone && in.enter != 0) {
        auto* field = s.ent[s.focused].try_get_mut<UiField>();
        if (field != nullptr && field->multi != 0 && field->len < 95 && field->readOnly == 0) {
            fieldRemember(s, s.focused, *field);
            fieldInsert(*field, "\n", 1);
            s.visualDirty = true;
        } else {
            activate(s, s.focused);
        }
    }

    if (s.focused != kUiNone && s.ent[s.focused].try_get<UiField>() != nullptr) {
        auto* field = s.ent[s.focused].try_get_mut<UiField>();
        const UiPaint* fp = s.ent[s.focused].try_get<UiPaint>();
        if (fp == nullptr || fp->disabled != 0) {
            return UiEvent::None;
        }
        bool edited = false;
        if (in.undo != 0) {
            edited = fieldUndo(s);
        } else if (in.redo != 0) {
            edited = fieldRedo(s);
        } else {
        const bool typing = in.selectAll != 0 || in.cut != 0 || (in.pasteOn != 0 && in.pasteLen > 0) || (in.ctrl == 0 && in.textLen > 0)
            || in.backspace != 0 || in.del != 0;
        if (typing) {
            fieldRemember(s, s.focused, *field);
        }
        auto move = [&](int dir) {
            if (in.shift == 0 && field->anchor != field->caret) {
                const uint8_t edge = dir < 0
                    ? (field->anchor < field->caret ? field->anchor : field->caret)
                    : (field->anchor > field->caret ? field->anchor : field->caret);
                field->caret = edge;
                field->anchor = edge;
                return;
            }
            if (dir < 0) {
                uint8_t prev = field->caret > 0 ? static_cast<uint8_t>(field->caret - 1) : 0;
                while (prev > 0 && (static_cast<unsigned char>(field->bytes[prev]) & 0xC0) == 0x80) {
                    --prev;
                }
                field->caret = prev;
            } else if (field->caret < field->len) {
                uint8_t step = 1;
                uiReadUtf8(field->bytes, field->len, field->caret, step);
                field->caret = static_cast<uint8_t>(field->caret + (step == 0 ? 1 : step));
            }
            if (in.shift == 0) {
                field->anchor = field->caret;
            }
        };
        if (in.selectAll != 0) {
            field->anchor = 0;
            field->caret = field->len;
            edited = true;
        }
        if (in.copy != 0 || in.cut != 0) {
            fieldCopy(s, *field);
            if (in.cut != 0) {
                fieldDropSel(*field);
                edited = true;
            }
        }
        if (in.pasteOn != 0 && in.pasteLen > 0) {
            fieldInsert(*field, in.paste, in.pasteLen);
            edited = true;
        }
        if (in.ctrl == 0 && in.textLen > 0) {
            fieldInsert(*field, in.text, in.textLen);
            edited = true;
        }
        if (in.backspace != 0) {
            fieldEraseBack(*field);
            edited = true;
        }
        if (in.del != 0) {
            fieldEraseForward(*field);
            edited = true;
        }
        if (in.left != 0) {
            move(-1);
            edited = true;
        }
        if (in.right != 0) {
            move(1);
            edited = true;
        }
        if (field->multi != 0 && (in.up != 0 || in.down != 0)) {
            const float viewW = s.box[s.focused].w - field->padL - field->padR;
            FieldRow rows[16]{};
            const int rn = fieldRows(s, *field, viewW, rows, 16);
            const float lineH = s.look.glyphH > 1.0f ? s.look.glyphH : 18.0f;
            int line = 0;
            for (int li = 0; li < rn; ++li) {
                if (field->caret >= rows[li].begin && field->caret <= rows[li].end) {
                    line = li;
                }
            }
            const float x = fieldOffset(s, *field, rows[line].begin, field->caret);
            line += in.up != 0 ? -1 : 1;
            if (line < 0) {
                line = 0;
            }
            if (line >= rn) {
                line = rn - 1;
            }
            field->caret = fieldCaretAt2(s, *field, x, static_cast<float>(line) * lineH + 1.0f, viewW);
            if (in.shift == 0) {
                field->anchor = field->caret;
            }
            edited = true;
        }
        }
        if (edited) {
            const float clearW = field->clear != 0 && field->len > 0 ? 22.0f : 0.0f;
            fieldReveal(s, *field, s.box[s.focused].w - field->padL - field->padR - clearW);
            fieldMark(s, s.focused);
            if (const UiEdit* edit = s.ent[s.focused].try_get<UiEdit>(); edit != nullptr && edit->fn != nullptr) {
                edit->fn(edit->user, s.focused);
            }
            const char* msg = "";
            if (fieldProblem(*field, msg) && s.statusLabel != kUiNone) {
                auto* text = s.ent[s.statusLabel].try_get_mut<UiText>();
                if (text != nullptr) {
                    copyText(text->bytes, text->len, msg);
                }
            }
            s.visualDirty = true;
        }
    }
    if (s.capture != kUiNone) {
        const UiPaint* cap = s.ent[s.capture].try_get<UiPaint>();
        if (cap != nullptr && cap->tag == -5 && s.laidW > 1.0f && s.laidH > 1.0f) {
            s.clipOn = 1;
            s.clipBox = {0, 0, s.laidW, s.laidH};
        }
    } else {
        s.clipOn = 0;
    }
    uiSyncInput(s);
    return UiEvent::None;
}

} // namespace burnhope
