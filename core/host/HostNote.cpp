#include "host/HostNote.hpp"
#include "ui/State.hpp"

namespace burnhope {

UiHostNote& uiHostNote(UiState& s) {
    return s.world.ensure<UiHostNote>();
}

UiPick& uiPick(UiState& s) {
    return s.world.ensure<UiPick>();
}

} // namespace burnhope
