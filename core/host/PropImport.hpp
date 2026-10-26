#pragma once

#include "host/PropBind.hpp"
#include "ui/Style.hpp"

#include <cstdint>
#include <string_view>

namespace burnhope {

struct PropSlot {
    const char* name = nullptr;
    uint16_t* id = nullptr;
};

struct PropCommand {
    const char* name = nullptr;
    void (*fn)(void* user, uint16_t item) = nullptr;
    void* user = nullptr;
};

using PropFlagFn = void (*)(void* user, uint16_t index, uint8_t value, uint16_t button, const char* label);

// Read one array from a layout document and build property rows into the binder.
// `key` selects the array on the root object. Strings in the document are copied
// into the binder, so the text does not have to stay mapped.
bool propImport(
    UiState& state,
    uint16_t parent,
    std::string_view text,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    void* user);

// propImport over a layout file resolved beside the executable (platformResolveAsset) and mapped read-only.
bool propImportAsset(
    UiState& state,
    uint16_t parent,
    const char* asset,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    void* user);

} // namespace burnhope
