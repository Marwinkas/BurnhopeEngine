#include "platform/Action.hpp"

#include "Str.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string_view>

namespace burnhope {
namespace {

ActionSlot gActions[32]{};
int gActionN = 0;
uint8_t gStack[8]{0};
int gStackN = 1;
char gListen[32]{};

bool modsMatch(const InputFrame& in, uint8_t mods) {
    const bool ctrl = (mods & 1u) != 0;
    const bool shift = (mods & 2u) != 0;
    const bool alt = (mods & 4u) != 0;
    const bool super = (mods & 8u) != 0;
    return (in.ctrl != 0) == ctrl && (in.shift != 0) == shift && (in.alt != 0) == alt && (in.super != 0) == super;
}

bool bitOn(const uint64_t* bits, int scan) {
    return scan >= 0 && scan < 512 && (bits[scan >> 6] & (1ull << (scan & 63))) != 0;
}

bool contextOpen(uint8_t context) {
    if (context == 0) {
        return true;
    }
    for (int i = gStackN - 1; i >= 0; --i) {
        if (gStack[i] == context) {
            return true;
        }
        if (gStack[i] == 1 && context != 1) {
            return false;
        }
    }
    return false;
}

ActionSlot* findAction(const char* name, bool make) {
    if (name == nullptr || name[0] == '\0') {
        return nullptr;
    }
    for (int i = 0; i < gActionN; ++i) {
        if (std::strcmp(gActions[i].name, name) == 0) {
            return &gActions[i];
        }
    }
    if (!make || gActionN >= 32) {
        return nullptr;
    }
    ActionSlot& slot = gActions[gActionN++];
    std::snprintf(slot.name, sizeof(slot.name), "%s", name);
    return &slot;
}

bool chordHot(const InputFrame& in, const Chord& chord, int trigger) {
    if (chord.trigger != trigger) {
        return false;
    }
    if (!modsMatch(in, chord.mods)) {
        return false;
    }
    if (chord.mouse != 0) {
        const uint32_t bit = chord.mouse;
        if (trigger == 0) {
            return (in.pointer.pressed & bit) != 0;
        }
        if (trigger == 1) {
            return (in.pointer.released & bit) != 0;
        }
        return (in.pointer.down & bit) != 0;
    }
    const int scan = chord.scan;
    if (trigger == 0) {
        return bitOn(in.keyEdge, scan);
    }
    if (trigger == 1) {
        return bitOn(in.keyUp, scan);
    }
    return bitOn(in.keyDown, scan);
}

bool actionFire(const InputFrame& in, const char* name, int trigger) {
    ActionSlot* slot = findAction(name, false);
    if (slot == nullptr || !contextOpen(slot->context)) {
        return false;
    }
    for (uint8_t i = 0; i < slot->chordN; ++i) {
        if (chordHot(in, slot->chord[i], trigger)) {
            return true;
        }
    }
    return false;
}

bool chordTaken(const char* self, int scan, uint8_t mods) {
    for (int i = 0; i < gActionN; ++i) {
        if (std::strcmp(gActions[i].name, self) == 0) {
            continue;
        }
        for (uint8_t c = 0; c < gActions[i].chordN; ++c) {
            if (gActions[i].chord[c].scan == scan && gActions[i].chord[c].mods == mods && gActions[i].chord[c].mouse == 0) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

void actionBindChord(const char* name, int scancode, uint8_t mods, uint8_t trigger, uint8_t mouse, uint8_t kind, uint8_t context) {
    ActionSlot* slot = findAction(name, true);
    if (slot == nullptr || slot->chordN >= 4) {
        return;
    }
    Chord& chord = slot->chord[slot->chordN++];
    chord.scan = scancode < 0 ? 0 : static_cast<uint16_t>(scancode);
    chord.mods = mods;
    chord.trigger = trigger;
    chord.mouse = mouse;
    slot->kind = kind;
    slot->context = context;
}

void actionBind(const char* name, int scancode, uint8_t mods) {
    actionBindChord(name, scancode, mods, 0, 0, 0, 0);
}

bool actionEdge(const InputFrame& in, const char* name) {
    return actionFire(in, name, 0);
}

bool actionHeld(const InputFrame& in, const char* name) {
    return actionFire(in, name, 2);
}

bool actionReleased(const InputFrame& in, const char* name) {
    return actionFire(in, name, 1);
}

float actionAxis(const InputFrame& in, const char* name) {
    ActionSlot* slot = findAction(name, false);
    if (slot == nullptr || slot->kind == 0 || !contextOpen(slot->context)) {
        return 0.0f;
    }
    for (uint8_t i = 0; i < slot->chordN; ++i) {
        if (slot->chord[i].mouse == 255) {
            return in.wheel;
        }
        if (slot->chord[i].mouse == 254) {
            return in.pointer.dx;
        }
    }
    return 0.0f;
}

void actionPush(uint8_t context) {
    if (gStackN < 8) {
        gStack[gStackN++] = context;
    }
}

void actionPop() {
    if (gStackN > 1) {
        --gStackN;
    }
}

void actionListen(const char* name) {
    gListen[0] = '\0';
    if (name != nullptr) {
        std::snprintf(gListen, sizeof(gListen), "%s", name);
    }
}

void actionOfferKey(int scan, uint8_t mods) {
    if (gListen[0] == '\0' || scan < 0 || scan >= 512) {
        return;
    }
    if (chordTaken(gListen, scan, mods)) {
        return;
    }
    ActionSlot* slot = findAction(gListen, true);
    if (slot == nullptr) {
        return;
    }
    slot->chordN = 0;
    actionBindChord(gListen, scan, mods, 0, 0, slot->kind, slot->context);
    gListen[0] = '\0';
}

bool actionSave(const char* path) {
    if (path == nullptr) {
        return false;
    }
    SDL_IOStream* file = SDL_IOFromFile(path, "w");
    if (file == nullptr) {
        return false;
    }
    for (int i = 0; i < gActionN; ++i) {
        for (uint8_t c = 0; c < gActions[i].chordN; ++c) {
            const Chord& chord = gActions[i].chord[c];
            SDL_IOprintf(
                file,
                "{\"name\":\"%s\",\"kind\":%u,\"ctx\":%u,\"scan\":%u,\"mods\":%u,\"trig\":%u,\"mouse\":%u}\n",
                gActions[i].name,
                gActions[i].kind,
                gActions[i].context,
                chord.scan,
                chord.mods,
                chord.trigger,
                chord.mouse);
        }
    }
    SDL_CloseIO(file);
    return true;
}

bool actionLoad(const char* path) {
    if (path == nullptr) {
        return false;
    }
    SDL_IOStream* file = SDL_IOFromFile(path, "r");
    if (file == nullptr) {
        return false;
    }
    char line[160]{};
    int n = 0;
    while (n < 159) {
        char ch = 0;
        if (SDL_ReadIO(file, &ch, 1) != 1) {
            break;
        }
        if (ch == '\n') {
            line[n] = '\0';
            char name[32]{};
            unsigned f[6]{};
            const char* cur = line;
            const char* const end = line + n;
            bool ok = strEat(cur, end, "{\"name\":\"");
            size_t len = 0;
            while (ok && cur < end && *cur != '"' && len + 1 < sizeof(name)) {
                name[len++] = *cur++;
            }
            ok = ok && strEat(cur, end, "\"");
            constexpr std::string_view keys[6] = {",\"kind\":", ",\"ctx\":", ",\"scan\":", ",\"mods\":", ",\"trig\":", ",\"mouse\":"};
            for (uint8_t i = 0; i < 6 && ok; ++i) {
                ok = strEat(cur, end, keys[i]) && strScanInt(cur, end, f[i]);
            }
            if (ok && strEat(cur, end, "}")) {
                actionBindChord(name, static_cast<int>(f[2]), static_cast<uint8_t>(f[3]), static_cast<uint8_t>(f[4]), static_cast<uint8_t>(f[5]), static_cast<uint8_t>(f[0]), static_cast<uint8_t>(f[1]));
            }
            n = 0;
            continue;
        }
        line[n++] = ch;
    }
    SDL_CloseIO(file);
    return true;
}

} // namespace burnhope
