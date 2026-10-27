#include "ui/State.hpp"

namespace burnhope {


void uiFocus(UiState& s, uint16_t id) {
    if (s.focused == id) {
        return;
    }
    s.focused = id;
    s.visualDirty = true;
    if (s.focusFn != nullptr) {
        s.focusFn(s.focusUser, id);
    }
}

void uiFocusStep(UiState& s, int dir) {
    if (s.count == 0) {
        return;
    }
    uint16_t ids[kUiCap];
    uint16_t keys[kUiCap];
    uint16_t n = 0;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (!s.ent[id].has<UiFocusable>() || !nodeShown(s, id)) {
            continue;
        }
        if (paint != nullptr && paint->disabled) {
            continue;
        }
        const UiTab* tab = s.ent[id].try_get<UiTab>();
        ids[n] = id;
        keys[n] = tab != nullptr ? tab->index : id;
        ++n;
    }
    if (n == 0) {
        return;
    }
    for (uint16_t i = 1; i < n; ++i) {
        const uint16_t id = ids[i];
        const uint16_t key = keys[i];
        uint16_t j = i;
        while (j > 0 && (keys[j - 1] > key || (keys[j - 1] == key && ids[j - 1] > id))) {
            ids[j] = ids[j - 1];
            keys[j] = keys[j - 1];
            --j;
        }
        ids[j] = id;
        keys[j] = key;
    }
    int at = 0;
    for (uint16_t i = 0; i < n; ++i) {
        if (ids[i] == s.focused) {
            at = static_cast<int>(i);
            break;
        }
    }
    const int step = dir < 0 ? -1 : 1;
    int next = at + step;
    if (s.focused == kUiNone) {
        next = dir < 0 ? static_cast<int>(n) - 1 : 0;
    }
    if (next < 0) {
        next = static_cast<int>(n) - 1;
    }
    if (next >= static_cast<int>(n)) {
        next = 0;
    }
    uiFocus(s, ids[next]);
}

} // namespace burnhope
