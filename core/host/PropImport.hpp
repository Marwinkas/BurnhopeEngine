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

// What a PropRole receives from the layout node that names it. `slot`, `channel`, `cell`, `kind` and `group` are
// the node's own integer keys; their meaning belongs to the role.
struct PropSpec {
    const char* label = nullptr;
    const char* name = nullptr;
    uint16_t bind = 0;
    float value = 0.0f;
    float minV = 0.0f;
    float maxV = 1.0f;
    uint8_t kind = 0;
    uint8_t group = 0;
    uint8_t slot = 0;
    uint8_t channel = 0;
    uint8_t cell = 0;
};

// A node role the host builds itself. Returns the node the layout should remember under the spec's name.
struct PropRole {
    const char* role = nullptr;
    uint16_t (*make)(void* user, UiState& state, UiPropBinder& bind, uint16_t parent, const PropSpec& spec) = nullptr;
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
    void* user,
    const PropRole* roles = nullptr,
    uint16_t roleCount = 0);

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
    void* user,
    const PropRole* roles = nullptr,
    uint16_t roleCount = 0);

} // namespace burnhope
