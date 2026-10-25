#pragma once

#include "ui/Interaction.hpp"
#include "platform/Input.hpp"
#include "ui/Types.hpp"
#include "ui/widget/Chrome.hpp"

namespace burnhope {

struct UiDock {
    uint16_t home = kUiNone;
    uint16_t panel = kUiNone;
    uint8_t edges = 15;
    uint8_t edge = 0;
    uint8_t hintOn = 0;
    uint8_t edgeOn = 0;
    UiBox hint{};
    UiBox edgeLine{};
};

UiDock* uiDockFind(UiState& s);

const UiDock* uiDockFind(const UiState& s);

void uiDockBind(UiState& s, uint16_t home, uint16_t panel, uint8_t edges);

void uiDockTo(UiState& s, uint8_t edge);
void uiInstallDockOverlay(UiState& s);
bool uiDockInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope
