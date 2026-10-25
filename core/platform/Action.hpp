#pragma once

#include "platform/Input.hpp"

#include <cstdint>

namespace burnhope {

struct Chord {
    uint16_t scan = 0;
    uint8_t mods = 0;
    uint8_t trigger = 0;
    uint8_t mouse = 0;
};

struct ActionSlot {
    char name[32]{};
    Chord chord[4]{};
    uint8_t chordN = 0;
    uint8_t kind = 0;
    uint8_t context = 0;
};

void actionBind(const char* name, int scancode, uint8_t mods);
void actionBindChord(const char* name, int scancode, uint8_t mods, uint8_t trigger, uint8_t mouse, uint8_t kind, uint8_t context);
[[nodiscard]] bool actionEdge(const InputFrame& in, const char* name);
[[nodiscard]] bool actionHeld(const InputFrame& in, const char* name);
[[nodiscard]] bool actionReleased(const InputFrame& in, const char* name);
[[nodiscard]] float actionAxis(const InputFrame& in, const char* name);
void actionPush(uint8_t context);
void actionPop();
void actionListen(const char* name);
void actionOfferKey(int scan, uint8_t mods);
[[nodiscard]] bool actionSave(const char* path);
[[nodiscard]] bool actionLoad(const char* path);

} // namespace burnhope
