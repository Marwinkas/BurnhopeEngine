#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"
#include "app/SceneBlockFill.hpp"

#include "ui/Icon.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {

namespace {

struct MatHome {
    uint16_t index = 0;
    float home = 0.0f;
    float home2 = 0.0f;
    uint8_t kind = 0;
};

void onMatReset(void* user, uint16_t id);

int nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int byteOf(float v) {
    int i = static_cast<int>(v * 255.0f + 0.5f);
    if (i < 0) {
        i = 0;
    }
    if (i > 255) {
        i = 255;
    }
    return i;
}

bool parseHex(const char* text, float& r, float& g, float& b, float& a, bool& hasA) {
    if (text == nullptr) {
        return false;
    }
    if (*text == '#') {
        ++text;
    }
    int n = 0;
    while (text[n] != '\0' && n < 8) {
        if (nibble(text[n]) < 0) {
            return false;
        }
        ++n;
    }
    if (text[n] != '\0' || (n != 6 && n != 8)) {
        return false;
    }
    const auto chan = [&](int i) { return (nibble(text[i * 2]) << 4) + nibble(text[i * 2 + 1]); };
    r = static_cast<float>(chan(0)) / 255.0f;
    g = static_cast<float>(chan(1)) / 255.0f;
    b = static_cast<float>(chan(2)) / 255.0f;
    hasA = n == 8;
    if (hasA) {
        a = static_cast<float>(chan(3)) / 255.0f;
    }
    return true;
}

void forceScalar(HudContext& hud, UiState& s, uint16_t index, float value) {
    if (hud.bind.scalars != nullptr && index < hud.bind.scalarN) {
        hud.bind.scalars[index] = value;
    }
    sceneBlockPut(hud, index, value);
    const uint16_t id = hud.bind.idOf(index, kBindFloat);
    if (id < s.count) {
        if (auto* range = s.ent[id].try_get_mut<UiRange>()) {
            range->value = value;
        }
        char text[32]{};
        uiWriteNumber(text, sizeof(text), value);
        uiSetFieldText(s, id, text);
    }
    for (uint8_t i = 0; i < kMatSlotN; ++i) {
        if (hud.matLink[i].index == index && hud.matLink[i].slider != 0 && hud.matLink[i].slider != kUiNone) {
            uiRangeSet(s, hud.matLink[i].slider, value);
        }
    }
}

} // namespace

UiState* matPageState(HudContext& hud) {
    if (hud.tool.canvas != nullptr && hud.tool.canvas->state != nullptr) {
        return hud.tool.canvas->state.get();
    }
    return hudUi(hud);
}

void matSetChoice(UiState& s, uint16_t id, uint8_t index, const char* label) {
    if (id == 0 || id >= s.count) {
        return;
    }
    auto* choice = s.ent[id].try_get_mut<UiChoice>();
    if (choice != nullptr && choice->index == index) {
        return;
    }
    if (choice != nullptr) {
        choice->index = index;
    }
    uiMenuCaption(s, id, label);
}

void matHexText(char* dst, std::size_t cap, float r, float g, float b, float a, bool withA) {
    if (withA) {
        std::snprintf(dst, cap, "#%02X%02X%02X%02X", byteOf(r), byteOf(g), byteOf(b), byteOf(a));
    } else {
        std::snprintf(dst, cap, "#%02X%02X%02X", byteOf(r), byteOf(g), byteOf(b));
    }
}

void matApplyFlag(HudContext& hud, UiState& s, uint16_t index, uint8_t value) {
    if (index < 64u) {
        hud.block.flags[index] = value;
    }
    switch (index) {
    case kFlMatWorld: hud.matDoc.worldAligned = value; break;
    case kFlMatDouble: hud.matDoc.doubleSided = value; break;
    case kFlMatFlip: hud.matDoc.flipNormalY = value; break;
    case kFlMatUnlit: hud.matDoc.unlit = value; break;
    case kFlMatRepeat: hud.matDoc.repeat = value; break;
    case kFlMatPack: hud.matDoc.mapPack = value; break;
    default: break;
    }
    uiCheckAssign(s, hud.bind.idOf(index, kBindFlag), value);
    if (index == kFlMatUnlit) {
        matSetChoice(s, hud.matType, value, value != 0 ? "Unlit" : "Lit");
    }
    if (index == kFlMatPack) {
        matSetChoice(s, hud.matPack, value, value != 0 ? "ORMH" : "Classic");
        if (value == 0) {
            hud.matDoc.ormPath[0] = '\0';
        } else {
            hudCommitMaps(hud, 255);
        }
        hud.atlasDirty = 1;
    }
}

void matOnSlide(void* user, float value) {
    auto* link = static_cast<MatLink*>(user);
    if (link == nullptr || link->hud == nullptr) {
        return;
    }
    UiState* state = matPageState(*link->hud);
    if (state == nullptr) {
        return;
    }
    link->hud->bind.write(*state, link->index, value);
}

void matOnNumber(void* user, float value) {
    auto* link = static_cast<MatLink*>(user);
    if (link == nullptr || link->hud == nullptr) {
        return;
    }
    UiState* state = matPageState(*link->hud);
    if (state != nullptr) {
        uiRangeSet(*state, link->slider, value);
    }
}

void matHexEdited(UiState& s, uint16_t id) {
    const MatHex* hex = id < s.count ? s.ent[id].try_get<MatHex>() : nullptr;
    const UiField* field = id < s.count ? s.ent[id].try_get<UiField>() : nullptr;
    if (hex == nullptr || hex->hud == nullptr || field == nullptr) {
        return;
    }
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    bool hasA = false;
    if (!parseHex(field->bytes, r, g, b, a, hasA)) {
        return;
    }
    forceScalar(*hex->hud, s, hex->index, r);
    forceScalar(*hex->hud, s, static_cast<uint16_t>(hex->index + 1u), g);
    forceScalar(*hex->hud, s, static_cast<uint16_t>(hex->index + 2u), b);
    if (hasA && hex->index == kScMatBase0) {
        forceScalar(*hex->hud, s, kScMatOpacity, a);
    }
    uiPaintRgb(s, hex->chip, r, g, b);
    hex->hud->previewDirty = 1;
    hex->hud->atlasDirty = 1;
    saveOpenMaterial(*hex->hud);
}

void matOnClear(void* user, uint16_t) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || hit->kind < 1) {
        return;
    }
    const uint8_t channel = static_cast<uint8_t>(hit->kind - 1u);
    hudDocPath(hit->hud->matDoc, channel)[0] = '\0';
    hudCommitMaps(*hit->hud, channel);
    hit->hud->atlasDirty = 1;
    hit->hud->previewDirty = 1;
    hudPaintMaterial(*hit->hud);
    saveOpenMaterial(*hit->hud);
}

void matOnPath(void* user, const char* path) {
    auto* hit = static_cast<MatHit*>(user);
    if (hit == nullptr || hit->hud == nullptr || path == nullptr || hit->kind < 1) {
        return;
    }
    hudTakeTexture(*hit->hud, static_cast<uint8_t>(hit->kind - 1u), path);
}

uint16_t matResetBtn(UiState& s, HudContext& hud, uint16_t parent, uint16_t index, float home, float home2, uint8_t kind) {
    const uint16_t id = uiIconBtn(s, parent, static_cast<uint8_t>(UiIcon::Reset), 22.0f, nullptr);
    if (id >= s.count) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->shrink = 0.0f;
    }
    s.ent[id].set<MatHome>({index, home, home2, kind});
    uiBind(s, id, onMatReset, &hud);
    return id;
}

namespace {

void onMatReset(void* user, uint16_t id) {
    auto* hud = static_cast<HudContext*>(user);
    UiState* state = hud != nullptr ? matPageState(*hud) : nullptr;
    if (hud == nullptr || state == nullptr || id >= state->count) {
        return;
    }
    const MatHome* home = state->ent[id].try_get<MatHome>();
    if (home == nullptr) {
        return;
    }
    if (home->kind == 1) {
        matApplyFlag(*hud, *state, home->index, home->home >= 0.5f ? 1u : 0u);
    } else if (home->kind == 6) {
        hudDocPath(hud->matDoc, static_cast<uint8_t>(home->index))[0] = '\0';
        hudCommitMaps(*hud, static_cast<uint8_t>(home->index));
        hud->atlasDirty = 1;
        hud->previewDirty = 1;
    } else if (home->kind == 2) {
        forceScalar(*hud, *state, home->index, home->home);
        forceScalar(*hud, *state, static_cast<uint16_t>(home->index + 1u), home->home2);
    } else if (home->kind == 3) {
        forceScalar(*hud, *state, home->index, 1.0f);
        forceScalar(*hud, *state, static_cast<uint16_t>(home->index + 1u), 1.0f);
        forceScalar(*hud, *state, static_cast<uint16_t>(home->index + 2u), 1.0f);
        const uint16_t chip = hud->bind.idOf(home->index, kBindSwatch);
        uiPaintRgb(*state, chip, 1.0f, 1.0f, 1.0f);
        hud->previewDirty = 1;
        hud->atlasDirty = 1;
    } else {
        forceScalar(*hud, *state, home->index, home->home);
    }
    hudPaintMaterial(*hud);
    saveOpenMaterial(*hud);
}

} // namespace

} // namespace burnhope
