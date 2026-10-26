#include "app/Engine.hpp"
#include "app/HudBuild.hpp"
#include "app/MatPriv.hpp"
#include "app/SceneBlockFill.hpp"

#include "color/Color.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/State.hpp"

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

} // namespace

UiState* matPageState(HudContext& hud) {
    if (hud.tool.canvas != nullptr && hud.tool.canvas->state != nullptr) {
        return hud.tool.canvas->state;
    }
    return hudUi(hud);
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
        uiChoiceSelect(s, hud.matType, value, value != 0 ? "Unlit" : "Lit");
    }
    if (index == kFlMatPack) {
        uiChoiceSelect(s, hud.matPack, value, value != 0 ? "ORMH" : "Classic");
        if (value == 0) {
            hud.matDoc.ormPath[0] = '\0';
        } else {
            hudCommitMaps(hud, 255);
        }
        hud.atlasDirty = 1;
    }
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
    uiSetGrow(s, id, -1.0f, 0.0f);
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
        hud->bind.write(*state, home->index, home->home);
        hud->bind.write(*state, static_cast<uint16_t>(home->index + 1u), home->home2);
    } else if (home->kind == 3) {
        hud->bind.write(*state, home->index, 1.0f);
        hud->bind.write(*state, static_cast<uint16_t>(home->index + 1u), 1.0f);
        hud->bind.write(*state, static_cast<uint16_t>(home->index + 2u), 1.0f);
        hud->previewDirty = 1;
        hud->atlasDirty = 1;
    } else {
        hud->bind.write(*state, home->index, home->home);
    }
    hudPaintMaterial(*hud);
    saveOpenMaterial(*hud);
}

} // namespace

} // namespace burnhope
